//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HLSStream.h"

#include <algorithm>
#include <unordered_set>

#include "CoroutineScheduler.hpp"
#include "DataView.hpp"
#include "HttpUtil.hpp"
#include "Log.hpp"
#include "LogConfig.h"
#include "M3U8Parser.h"

namespace slark {

namespace {
inline uint64_t logProgressBytes() {
    return getLogConfig().progressLogBytes;
}
}  // namespace

HLSStream::HLSStream() = default;

HLSStream::~HLSStream() {
    close();
}

StreamResult<StreamInfo> HLSStream::open(const OpenParams& params) {
    params_ = params;

    if (params.onMetadata) {
        metadataCallback_ = params.onMetadata;
    }

    auto lastSlash = params.uri.rfind('/');
    if (lastSlash != std::string::npos) {
        baseUrl_ = params.uri.substr(0, lastSlash + 1);
    }

    info_.type = StreamType::HLS;
    info_.uri = params.uri;
    info_.isSeekable = true;
    info_.isLive = false;

    state_ = StreamState::Opening;
    return info_;
}

void HLSStream::close() noexcept {
    LogI("[HLSStream::close] url={} pos={} state={}",
         params_.uri,
         position_.load(),
         static_cast<int>(state_.load()));
    if (request_) {
        request_->cancel();
        request_.reset();
    }

    state_ = StreamState::Idle;
    isRunning_ = false;
    isCompleted_ = false;
}

void HLSStream::storeTask(Task<void>&& task) noexcept {
    pendingTasks_.erase(std::remove_if(pendingTasks_.begin(),
                                       pendingTasks_.end(),
                                       [](const Task<void>& t) {
                                           return t.isDone();
                                       }),
                        pendingTasks_.end());
    pendingTasks_.push_back(std::move(task));
}

void HLSStream::start() noexcept {
    if (state_ != StreamState::Ready && state_ != StreamState::Paused &&
        state_ != StreamState::Opening) {
        LogI("[HLSStream::start] skip, state={}", static_cast<int>(state_.load()));
        return;
    }

    // 立即标记 Reading，防止并发重复启动
    state_ = StreamState::Reading;
    isPaused_ = false;
    isRunning_ = true;

    LogI("[HLSStream::start] url={}", params_.uri);

    request_ = std::make_shared<http::HttpRequest>();

    http::RequestInfo requestInfo;
    requestInfo.url = params_.uri;
    requestInfo.methodType = http::HttpMethodType::Get;

    auto self = shared_from_this();

    auto fetchPlaylistTask = [](std::shared_ptr<http::HttpRequest> req,
                                http::RequestInfo info,
                                std::shared_ptr<HLSStream> stream) -> Task<void> {
        auto response = co_await req->asyncExecute(info);

        if (response.isOk()) {
            stream->handlePlaylistResponse(std::move(response));
        } else {
            stream->reportHttpError(response);
        }
    };

    auto task = fetchPlaylistTask(request_, requestInfo, self);
    storeTask(std::move(task));
    pendingTasks_.back().getHandle().resume();
}

void HLSStream::pause() noexcept {
    if (state_ != StreamState::Reading) {
        LogI("[HLSStream::pause] noop, state={}", static_cast<int>(state_.load()));
        return;
    }

    isPaused_ = true;
    state_ = StreamState::Paused;
    isRunning_ = false;

    LogI("[HLSStream::pause] pos={}", position_.load());
}

StreamResult<void> HLSStream::seek(uint64_t /*position*/) {
    return std::unexpected(Error::InvalidOperation);
}

StreamResult<void> HLSStream::seekByTime(double timeSec) {
    LogI("[HLSStream::seekByTime] time={}", timeSec);
    if (!playlist_ || playlist_->segments().empty()) {
        return std::unexpected(Error::StreamInvalidData);
    }
    if (playlist_->isLive()) {
        return std::unexpected(Error::InvalidOperation);
    }
    auto segOpt = playlist_->findSegmentByTime(timeSec);
    if (!segOpt) {
        return std::unexpected(Error::OutOfRange);
    }
    currentSegmentIndex_.store(*segOpt);
    position_ = 0;
    if (request_) {
        request_->cancel();
        request_.reset();
    }
    if (isRunning_) {
        downloadSegments();
    }
    return {};
}

void HLSStream::updateRange(Range) noexcept {}

void HLSStream::handlePlaylistResponse(http::HttpResponse&& response) {
    LogI("[HLSStream::handlePlaylistResponse] received: status={} bodySize={}",
         static_cast<int>(response.statusCode),
         response.body ? response.body->size() : 0);

    if (!response.body) {
        if (params_.onError) {
            params_.onError(Error::StreamInvalidData, 0);
        }
        state_ = StreamState::Error;
        return;
    }

    auto content = response.body->toString();

    if (!parser_) {
        parser_ = std::make_unique<M3U8Parser>(baseUrl_);
    }

    if (!parser_->parse(content)) {
        LogE("[HLSStream::handlePlaylistResponse] M3U8 parse failed: bodySize={}", content.size());
        if (params_.onError) {
            params_.onError(Error::StreamInvalidData, 0);
        }
        state_ = StreamState::Error;
        return;
    }

    playlist_ = parser_->playlist();

    if (parser_->isMasterPlaylist()) {
        LogI("[HLSStream::handlePlaylistResponse] master playlist parsed, selecting variant");
        auto variantUri = selectVariantUri(0);
        if (!variantUri) {
            LogE("[HLSStream::handlePlaylistResponse] no variant available");
            state_ = StreamState::Error;
            return;
        }

        auto variantUrl = resolveUrl(*variantUri);
        mediaPlaylistUrl_ = variantUrl;
        LogI("[HLSStream::handlePlaylistResponse] variant selected: {}", variantUrl);

        parser_.reset();
        request_ = std::make_shared<http::HttpRequest>();

        http::RequestInfo requestInfo;
        requestInfo.url = variantUrl;
        requestInfo.methodType = http::HttpMethodType::Get;

        auto self = shared_from_this();

        auto fetchVariantTask = [](std::shared_ptr<http::HttpRequest> req,
                                   http::RequestInfo info,
                                   std::shared_ptr<HLSStream> stream) -> Task<void> {
            auto response = co_await req->asyncExecute(info);

            if (response.isOk()) {
                stream->handlePlaylistResponse(std::move(response));
            } else {
                stream->reportHttpError(response);
            }
        };

        auto task = fetchVariantTask(request_, requestInfo, self);
        storeTask(std::move(task));
        pendingTasks_.back().getHandle().resume();
    } else {
        info_.isLive = playlist_->isLive();
        info_.isSeekable = !playlist_->isLive();
        info_.totalSize = static_cast<uint64_t>(playlist_->totalDuration() * 1000);

        LogI("[HLSStream::handlePlaylistResponse] media playlist parsed: segments={} isLive={} "
             "totalDuration={}",
             playlist_->segments().size(),
             info_.isLive,
             playlist_->totalDuration());

        if (metadataCallback_) {
            metadataCallback_(std::static_pointer_cast<void>(playlist_));
        }

        if (mediaPlaylistUrl_.empty()) {
            mediaPlaylistUrl_ = params_.uri;
        }

        state_ = StreamState::Ready;
        downloadSegments();
    }
}

std::optional<std::string> HLSStream::selectVariantUri(uint32_t bandwidth) {
    if (!playlist_ || !playlist_->hasVariants()) {
        return std::nullopt;
    }

    const auto& variants = playlist_->variants();
    auto selectedIt = variants.end();
    for (auto it = variants.begin(); it != variants.end(); ++it) {
        if (bandwidth == 0 || it->bandwidth <= bandwidth) {
            if (selectedIt == variants.end() || it->bandwidth > selectedIt->bandwidth) {
                selectedIt = it;
            }
        }
    }

    if (selectedIt == variants.end()) {
        selectedIt = variants.begin();
    }

    return selectedIt->uri;
}

void HLSStream::downloadSegments() {
    if (isPaused_ || !isRunning_ || !playlist_) {
        return;
    }

    const auto& segments = playlist_->segments();
    auto segIndex = currentSegmentIndex_.load();
    if (segIndex >= segments.size()) {
        if (playlist_->isLive()) {
            refreshPlaylist();
            return;
        }
        LogI("[HLSStream::downloadSegments] all segments done, total={}", segments.size());
        isCompleted_ = true;
        state_ = StreamState::Completed;
        isRunning_ = false;

        if (params_.onComplete) {
            params_.onComplete();
        }
        return;
    }

    downloadSegment(segIndex);
}

void HLSStream::downloadSegment(uint32_t segIndex) {
    constexpr int32_t kMaxRetry = 3;
    downloadSegmentWithRetry(segIndex, kMaxRetry);
}

void HLSStream::downloadSegmentWithRetry(uint32_t segIndex, int32_t retryLeft) {
    if (!playlist_ || segIndex >= playlist_->segments().size()) {
        return;
    }

    const auto& segment = playlist_->segments()[segIndex];
    LogI("[HLSStream::downloadSegmentWithRetry] start: segIndex={} retryLeft={} url={}",
         segIndex,
         retryLeft,
         resolveUrl(segment.uri));
    request_ = std::make_shared<http::HttpRequest>();

    http::RequestInfo requestInfo;
    requestInfo.url = resolveUrl(segment.uri);
    requestInfo.methodType = http::HttpMethodType::Get;

    auto self = shared_from_this();

    auto downloadTask = [](std::shared_ptr<http::HttpRequest> req,
                           http::RequestInfo info,
                           std::shared_ptr<HLSStream> stream,
                           uint32_t segIdx,
                           int32_t left) -> Task<void> {
        auto gen = req->streamingExecute(info, req->cancelToken());
        bool hasError = false;

        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }

            if (stream->isPaused_) {
                co_return;
            }

            if (chunk->hasError()) {
                hasError = true;
                stream->lastErrorOriginCode_.store(chunk->error.originErrorCode,
                                                   std::memory_order_release);
                break;
            }

            stream->handleSegmentChunk(std::move(chunk.value()), segIdx);
        }

        if (hasError) {
            if (left > 0) {
                LogW("HLSStream: segment {} download failed, retrying ({} left)", segIdx, left);
                stream->downloadSegmentWithRetry(segIdx, left - 1);
            } else {
                LogE("HLSStream: segment {} download failed after all retries", segIdx);
                if (stream->params_.onError) {
                    stream->params_.onError(
                        Error::NetworkError,
                        stream->lastErrorOriginCode_.load(std::memory_order_acquire));
                }
                stream->state_ = StreamState::Error;
            }
            co_return;
        }

        stream->currentSegmentIndex_.fetch_add(1, std::memory_order_relaxed);
        LogI("[HLSStream] segment {} done, next={}", segIdx, segIdx + 1);
        stream->downloadSegments();
    };

    auto task = downloadTask(request_, requestInfo, self, segIndex, retryLeft);
    storeTask(std::move(task));
    pendingTasks_.back().getHandle().resume();
}

void HLSStream::handleSegmentChunk(http::StreamChunk&& chunk, uint32_t segmentIndex) {
    if (chunk.hasData()) {
        auto chunkSize = chunk.data->length();
        auto buffer = std::shared_ptr<Data>(chunk.data.release());

        if (params_.onData) {
            params_.onData(DataChunk{
                .data = DataView(buffer->view()),
                .owner = std::move(buffer),
                .offset = position_,
                .segmentIndex = segmentIndex,
                .isLastChunk = chunk.isFinal,
            });
        }

        position_ += chunkSize;

        if (position_.load() / logProgressBytes() !=
            (position_.load() - chunkSize) / logProgressBytes()) {
            LogI("[HLSStream::handleSegmentChunk] progress: segIndex={} pos={} chunk={}",
                 segmentIndex,
                 position_.load(),
                 chunkSize);
        }
    }
}

void HLSStream::reportHttpError(const http::HttpResponse& response) {
    if (!params_.onError) {
        state_ = StreamState::Error;
        isRunning_ = false;
        return;
    }
    Error err = Error::NetworkError;
    switch (response.error.retCode) {
        case slark::Error::ConnectionTimeout:
        case slark::Error::HttpError:
        case slark::Error::Cancelled:
        case slark::Error::DnsResolveFailed:
        case slark::Error::ConnectionFailed:
            err = response.error.retCode;
            break;
        default:
            break;
    }
    LogE("[HLSStream::reportHttpError] err={} originCode={}",
         static_cast<int>(err),
         response.error.originErrorCode);
    params_.onError(err, response.error.originErrorCode);
    state_ = StreamState::Error;
    isRunning_ = false;
}

std::string HLSStream::resolveUrl(const std::string& relativeUrl) const {
    return M3U8Parser::resolveUrl(baseUrl_, relativeUrl);
}

void HLSStream::refreshPlaylist() {
    if (mediaPlaylistUrl_.empty() || isPaused_ || !isRunning_) {
        return;
    }

    LogI("[HLSStream::refreshPlaylist] url={}", mediaPlaylistUrl_);
    request_ = std::make_shared<http::HttpRequest>();

    http::RequestInfo requestInfo;
    requestInfo.url = mediaPlaylistUrl_;
    requestInfo.methodType = http::HttpMethodType::Get;

    auto self = shared_from_this();

    auto fetchTask = [](std::shared_ptr<http::HttpRequest> req,
                        http::RequestInfo info,
                        std::shared_ptr<HLSStream> stream) -> Task<void> {
        auto response = co_await req->asyncExecute(info);

        if (response.isOk()) {
            stream->handlePlaylistResponse(std::move(response));
        } else {
            stream->reportHttpError(response);
        }
    };

    auto task = fetchTask(request_, requestInfo, self);
    storeTask(std::move(task));
    pendingTasks_.back().getHandle().resume();
}

}  // namespace slark
