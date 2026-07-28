//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HttpStream.h"

#include "CoroutineScheduler.hpp"
#include "DataView.hpp"
#include "Log.hpp"
#include "LogConfig.h"

namespace slark {

namespace {
inline uint64_t logProgressBytes() {
    return getLogConfig().progressLogBytes;
}
}  // namespace

HttpStream::HttpStream() = default;

HttpStream::~HttpStream() {
    close();
}

auto HttpStream::open(const OpenParams& params) -> StreamResult<StreamInfo> {
    params_ = params;
    readRange_ = params.range;

    info_.type = StreamType::Http;
    info_.uri = params.uri;
    info_.isSeekable = false;
    info_.isLive = false;

    state_ = StreamState::Ready;
    return info_;
}

void HttpStream::close() noexcept {
    if (request_) {
        request_->cancel();
        request_.reset();
    }

    LogI("[HttpStream::close] url={} pos={} state={}",
         params_.uri,
         position_.load(),
         static_cast<int>(state_.load()));

    state_ = StreamState::Idle;
    isRunning_ = false;
    isCompleted_ = false;
}

void HttpStream::start() noexcept {
    if (state_ != StreamState::Ready && state_ != StreamState::Paused) {
        LogI("[HttpStream::start] skip, state={}", static_cast<int>(state_.load()));
        return;
    }

    // 立即标记 Reading，防止 seek() 异步 schedule 与 MediaSourceManager::start()
    // 并发调用导致重复启动（两个 start 各自建立连接，前一个完成后 release 连接，
    // 后一个 tryReuse 拿到已关闭的连接 → EOF → error）
    state_ = StreamState::Reading;
    isPaused_ = false;
    isRunning_ = true;

    // pause 后 resume 时 position_ 需要重置为 range 起始位置，
    // 否则新协程的 DataPacket.offset 会从旧 position_ 递增，导致
    // demuxer buffer offset 与实际文件偏移不匹配，视频帧无法被解析。
    if (readRange_.isValid()) {
        position_ = readRange_.start();
    }

    request_ = std::make_shared<http::HttpRequest>();

    http::RequestInfo requestInfo;
    requestInfo.url = params_.uri;
    requestInfo.methodType = http::HttpMethodType::Get;

    if (readRange_.isValid()) {
        requestInfo.headers["Range"] = readRange_.toHeaderString();
    }

    LogI("[HttpStream::start] url={} range={} method=GET",
         requestInfo.url,
         requestInfo.headers.count("Range") ? requestInfo.headers["Range"] : std::string{});

    auto self = request_;
    std::weak_ptr<HttpStream> weakThis = shared_from_this();

    auto streamTask = [](std::shared_ptr<http::HttpRequest> req,
                         http::RequestInfo info,
                         std::weak_ptr<HttpStream> weakStream) -> Task<void> {
        // 必须传 req->cancelToken()：seek 时 cancel() 才能中断 processStreaming
        auto gen = req->streamingExecute(info, req->cancelToken());

        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }

            auto stream = weakStream.lock();
            if (!stream) {
                co_return;
            }

            stream->handleChunk(std::move(chunk.value()));

            if (stream->isPaused_ || stream->isCompleted_ || stream->state_ == StreamState::Error) {
                co_return;
            }
        }

        if (auto stream = weakStream.lock()) {
            stream->isCompleted_ = true;
            stream->state_ = StreamState::Completed;
            stream->isRunning_ = false;

            LogI("[HttpStream::streamTask] generator exhausted, totalReceived={} url={}",
                 stream->position_.load(),
                 stream->params_.uri);

            if (stream->params_.onComplete) {
                stream->params_.onComplete();
            }
        }
    };

    auto task = streamTask(self, requestInfo, weakThis);
    streamTask_ = std::move(task);
    streamTask_.getHandle().resume();
}

void HttpStream::pause() noexcept {
    if (state_ != StreamState::Reading) {
        LogI("[HttpStream::pause] noop, state={}", static_cast<int>(state_.load()));
        return;
    }

    isPaused_ = true;
    state_ = StreamState::Paused;
    isRunning_ = false;

    LogI("[HttpStream::pause] pos={}", position_.load());
}

auto HttpStream::seek(uint64_t position) -> StreamResult<void> {
    if (!acceptRanges_) {
        LogW("[HttpStream::seek] reject, server does not advertise Accept-Ranges, pos={}",
             position);
        return std::unexpected(Error::StreamNotReady);
    }

    readRange_ = Range(position);
    position_ = position;

    LogI("[HttpStream::seek] pos={} url={}", position, params_.uri);

    if (request_) {
        request_->cancel();
        request_.reset();
    }
    {
        auto tmp = std::move(streamTask_);
    }

    isCompleted_ = false;
    state_ = StreamState::Ready;
    start();

    return {};
}

void HttpStream::updateRange(Range range) noexcept {
    readRange_ = range;
}

void HttpStream::handleChunk(http::StreamChunk&& chunk) noexcept {
    if (chunk.hasError()) {
        // retCode 已是 slark::Error，originErrorCode 携带真实子码（HTTP statusCode / errno）
        lastErrorOriginCode_.store(chunk.error.originErrorCode, std::memory_order_release);
        LogE("[HttpStream::handleChunk] error chunk: retCode={} originCode={} pos={} url={}",
             static_cast<int>(chunk.error.retCode),
             chunk.error.originErrorCode,
             position_.load(),
             params_.uri);
        // Cancelled 是 seek/cancel 的正常副作用，不上报为错误
        if (chunk.error.retCode != Error::Cancelled) {
            if (params_.onError) {
                params_.onError(chunk.error.retCode, chunk.error.originErrorCode);
            }
        }
        isRunning_ = false;
        state_ = StreamState::Error;
        return;
    }

    if (chunk.statusCode != http::HttpStatusCode::Unknown) {
        auto it = chunk.headers.find("Accept-Ranges");
        if (it != chunk.headers.end() && it->second == "bytes") {
            acceptRanges_ = true;
            info_.isSeekable = true;
        }

        auto lengthIt = chunk.headers.find("Content-Length");
        if (lengthIt != chunk.headers.end()) {
            auto lenResult = StringUtil::fromChars<uint64_t>(lengthIt->second);
            if (lenResult) {
                contentLength_ = *lenResult;
                info_.totalSize = contentLength_;
            }
        }

        auto rangeIt = chunk.headers.find("Content-Range");
        if (rangeIt != chunk.headers.end()) {
            const auto& rangeStr = rangeIt->second;
            auto slashPos = rangeStr.find('/');
            if (slashPos != std::string::npos) {
                auto totalStr = rangeStr.substr(slashPos + 1);
                auto totalResult = StringUtil::fromChars<uint64_t>(totalStr);
                if (totalResult) {
                    info_.totalSize = *totalResult;
                }
            }
        }

        LogI("[HttpStream::handleChunk] header: status={} acceptRanges={} contentLength={} "
             "totalSize={} url={}",
             static_cast<int>(chunk.statusCode),
             acceptRanges_ ? "bytes" : "none",
             contentLength_,
             info_.totalSize,
             params_.uri);

        return;
    }

    if (chunk.hasData()) {
        auto chunkSize = chunk.data->length();
        auto buffer = std::shared_ptr<Data>(chunk.data.release());

        if (params_.onData) {
            params_.onData(DataChunk{
                .data = DataView(buffer->view()),
                .owner = std::move(buffer),
                .offset = position_,
                .segmentIndex = 0,
                .isLastChunk = chunk.isFinal,
            });
        }

        position_ += chunkSize;

        // 限频进度日志：每累计 progressLogBytes 打一次，避免热路径 spam
        if (position_.load() / logProgressBytes() !=
            (position_.load() - chunkSize) / logProgressBytes()) {
            LogI("[HttpStream::handleChunk] progress: pos={} chunk={} url={}",
                 position_.load(),
                 chunkSize,
                 params_.uri);
        }
    }

    if (chunk.isFinal) {
        isCompleted_ = true;
        state_ = StreamState::Completed;
        isRunning_ = false;

        LogI("[HttpStream::handleChunk] final chunk: totalReceived={} url={}",
             position_.load(),
             params_.uri);

        if (params_.onComplete) {
            params_.onComplete();
        }
    }
}

void HttpStream::handleError(const http::ErrorInfo& error) noexcept {
    lastErrorOriginCode_.store(error.originErrorCode, std::memory_order_release);
    LogE("[HttpStream::handleError] retCode={} originCode={} pos={} url={}",
         static_cast<int>(error.retCode),
         error.originErrorCode,
         position_.load(),
         params_.uri);
    if (params_.onError) {
        params_.onError(error.retCode, error.originErrorCode);
    }

    state_ = StreamState::Error;
    isRunning_ = false;
}

}  // namespace slark
