//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HttpStream.h"

#include <chrono>
#include <thread>

#include "CoroutineScheduler.hpp"
#include "DataView.hpp"
#include "Log.hpp"
#include "LogConfig.h"

namespace slark {

namespace {
inline uint64_t logProgressBytes() {
    return getLogConfig().progressLogBytes;
}

constexpr std::chrono::milliseconds kWaitSettleTimeout{500};
constexpr std::chrono::milliseconds kCloseWaitTimeout{5000};
}  // namespace

HttpStream::HttpStream(std::shared_ptr<Thread> ownerThread)
    : ownerThread_(std::move(ownerThread)) {}

HttpStream::~HttpStream() {
    close();
}

auto HttpStream::open(const OpenParams& params) -> StreamResult<StreamInfo> {
    params_ = params;
    readRange_ = params.range;
    committedPosition_ = readRange_.isValid() ? readRange_.start() : 0;

    {
        std::lock_guard<std::mutex> lock(infoMutex_);
        info_.type = StreamType::Http;
        info_.uri = params.uri;
        info_.isSeekable = false;
        info_.isLive = false;
        info_.totalSize = 0;
    }

    state_ = StreamState::Ready;
    return info();
}

void HttpStream::close() noexcept {
    LogI("[HttpStream::close] url={} pos={} state={}", params_.uri, position_.load(),
         static_cast<int>(state_.load()));
    std::lock_guard<std::mutex> lock(mutex_);
    closing_ = true;
    ++generation_;
    cancelCurrentOperation(kCloseWaitTimeout.count());
    state_ = StreamState::Idle;
    isRunning_ = false;
    isCompleted_ = false;
    isPaused_ = false;
}

void HttpStream::start() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    startLocked();
}

void HttpStream::startLocked() noexcept {
    if (state_ != StreamState::Ready && state_ != StreamState::Paused) {
        LogI("[HttpStream::startLocked] skip, state={}", static_cast<int>(state_.load()));
        return;
    }
    if (closing_) {
        return;
    }

    ++generation_;
    cancelCurrentOperation(kWaitSettleTimeout.count());

    readRange_ = Range(committedPosition_);
    position_ = committedPosition_;
    state_ = StreamState::Reading;
    isPaused_ = false;
    isRunning_ = true;

    auto request = std::make_shared<http::HttpRequest>();
    auto control = std::make_shared<StreamControl>();
    uint64_t gen = generation_;
    uint64_t opId = nextOperationId_++;

    http::RequestInfo requestInfo;
    requestInfo.url = params_.uri;
    requestInfo.methodType = http::HttpMethodType::Get;
    if (readRange_.isValid()) {
        requestInfo.headers["Range"] = readRange_.toHeaderString();
    }

    LogI("[HttpStream::startLocked] url={} range={} gen={} method=GET", requestInfo.url,
         requestInfo.headers.count("Range") ? requestInfo.headers["Range"] : std::string{}, gen);

    std::weak_ptr<HttpStream> weakThis = shared_from_this();
    std::weak_ptr<StreamControl> weakControl = control;

    auto streamTask = [](std::shared_ptr<http::HttpRequest> req, http::RequestInfo info,
                         std::weak_ptr<HttpStream> weakStream,
                         std::weak_ptr<StreamControl> weakControl,
                         std::shared_ptr<Thread> ownerThread, uint64_t generation,
                         uint64_t operationId) -> Task<void> {
        auto gen = req->streamingExecute(info, req->cancelToken());
        while (true) {
            auto chunk = co_await gen.next();
            if (!chunk.has_value()) {
                break;
            }

            auto control = weakControl.lock();
            if (!control || !control->alive.load()) {
                co_return;
            }
            auto stream = weakStream.lock();
            if (!stream) {
                co_return;
            }

            auto event = HttpStream::StreamEvent{generation, operationId, std::move(*chunk)};
            if (!ownerThread->postTask([weakStream, event = std::move(event)]() mutable {
                    auto s = weakStream.lock();
                    if (!s) {
                        return;
                    }
                    s->onEvent(std::move(event));
                })) {
                co_return;
            }
        }
    };

    auto task = streamTask(request, requestInfo, weakThis, weakControl, ownerThread_, gen, opId);
    operation_.emplace(StreamOperation{gen, opId, request, std::move(task), std::move(control)});
    operation_->task.resume();
}

void HttpStream::pause() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != StreamState::Reading) {
        LogI("[HttpStream::pause] noop, state={}", static_cast<int>(state_.load()));
        return;
    }

    ++generation_;
    cancelCurrentOperation(kWaitSettleTimeout.count());

    state_ = StreamState::Paused;
    isRunning_ = false;
    LogI("[HttpStream::pause] committedPos={}", committedPosition_);
}

auto HttpStream::seek(uint64_t position) -> StreamResult<void> {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!acceptRanges_) {
        LogW("[HttpStream::seek] reject, server does not advertise Accept-Ranges, pos={}",
             position);
        return std::unexpected(Error::StreamNotReady);
    }

    ++generation_;
    cancelCurrentOperation(kWaitSettleTimeout.count());

    committedPosition_ = position;
    isCompleted_ = false;
    state_ = StreamState::Ready;
    LogI("[HttpStream::seek] pos={} url={}", position, params_.uri);
    startLocked();
    return {};
}

void HttpStream::updateRange(Range range) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    readRange_ = range;
}

void HttpStream::cancelCurrentOperation(uint64_t timeoutMs) noexcept {
    if (!operation_) {
        return;
    }
    operation_->request->cancel();
    operation_->control->alive = false;

    auto opTask = std::move(operation_->task);
    operation_.reset();
    mutex_.unlock();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!opTask.isDone()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            LogW("[HttpStream::cancelCurrentOperation] task not settled within {}ms", timeoutMs);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    mutex_.lock();
}

void HttpStream::onEvent(StreamEvent event) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.generation != generation_ || !operation_ ||
        event.operationId != operation_->operationId) {
        return;
    }
    auto& chunk = event.chunk;

    if (chunk.hasError()) {
        lastErrorOriginCode_.store(chunk.error.originErrorCode, std::memory_order_release);
        LogE("[HttpStream::onEvent] error chunk: retCode={} originCode={} pos={} url={}",
             static_cast<int>(chunk.error.retCode), chunk.error.originErrorCode, position_.load(),
             params_.uri);
        if (chunk.error.retCode == Error::Cancelled) {
            return;
        }
        handleError(chunk.error);
        return;
    }

    if (state_ == StreamState::Error) {
        return;
    }

    if (chunk.statusCode != http::HttpStatusCode::Unknown) {
        handleHeader(chunk);
        return;
    }

    if (chunk.hasData()) {
        handleData(std::move(event.chunk));
        return;
    }

    if (chunk.isFinal) {
        handleFinal();
    }
}

void HttpStream::handleError(const http::ErrorInfo& error) noexcept {
    lastErrorOriginCode_.store(error.originErrorCode, std::memory_order_release);
    LogE("[HttpStream::handleError] retCode={} originCode={} pos={} url={}",
         static_cast<int>(error.retCode), error.originErrorCode, position_.load(), params_.uri);
    if (params_.onError) {
        params_.onError(error.retCode, error.originErrorCode);
    }
    isRunning_ = false;
    state_ = StreamState::Error;
}

void HttpStream::handleHeader(const http::StreamChunk& chunk) noexcept {
    if (!validateRangeResponse(chunk)) {
        return;
    }

    auto it = chunk.headers.find("accept-ranges");
    if (it != chunk.headers.end() && it->second == "bytes") {
        acceptRanges_ = true;
        std::lock_guard<std::mutex> lock(infoMutex_);
        info_.isSeekable = true;
    } else {
        std::lock_guard<std::mutex> lock(infoMutex_);
        info_.isSeekable = false;
    }

    auto lengthIt = chunk.headers.find("content-length");
    if (lengthIt != chunk.headers.end()) {
        auto lenResult = StringUtil::fromChars<uint64_t>(lengthIt->second);
        if (lenResult) {
            contentLength_ = *lenResult;
            std::lock_guard<std::mutex> lock(infoMutex_);
            info_.totalSize = contentLength_;
        }
    }

    auto rangeIt = chunk.headers.find("content-range");
    if (rangeIt != chunk.headers.end()) {
        const auto& rangeStr = rangeIt->second;
        auto slashPos = rangeStr.find('/');
        if (slashPos != std::string::npos) {
            auto totalStr = rangeStr.substr(slashPos + 1);
            auto totalResult = StringUtil::fromChars<uint64_t>(totalStr);
            if (totalResult) {
                std::lock_guard<std::mutex> lock(infoMutex_);
                info_.totalSize = *totalResult;
            }
        }
    }

    LogI("[HttpStream::handleHeader] status={} acceptRanges={} contentLength={} totalSize={} "
         "url={}",
         static_cast<int>(chunk.statusCode), acceptRanges_ ? "bytes" : "none", contentLength_,
         info_.totalSize, params_.uri);
}

bool HttpStream::validateRangeResponse(const http::StreamChunk& chunk) noexcept {
    if (!readRange_.isValid()) {
        return true;
    }

    const auto kStatus = chunk.statusCode;
    if (kStatus == http::HttpStatusCode::RangeNotSatisfiable) {
        LogW("[HttpStream::validateRangeResponse] 416, requested range not satisfiable: {}",
             readRange_.toString());
        lastErrorOriginCode_.store(416, std::memory_order_release);
        if (params_.onError) {
            params_.onError(Error::OutOfRange, 416);
        }
        isRunning_ = false;
        state_ = StreamState::Error;
        return false;
    }

    if (kStatus != http::HttpStatusCode::PartialContent) {
        LogW("[HttpStream::validateRangeResponse] server ignored Range, status={} requested={}",
             static_cast<int>(kStatus), readRange_.toString());
        lastErrorOriginCode_.store(static_cast<int32_t>(kStatus), std::memory_order_release);
        if (params_.onError) {
            params_.onError(Error::StreamNotReady, static_cast<int32_t>(kStatus));
        }
        isRunning_ = false;
        state_ = StreamState::Error;
        return false;
    }

    auto rangeIt = chunk.headers.find("content-range");
    if (rangeIt == chunk.headers.end()) {
        LogW("[HttpStream::validateRangeResponse] 206 without Content-Range");
        lastErrorOriginCode_.store(0, std::memory_order_release);
        if (params_.onError) {
            params_.onError(Error::StreamInvalidData, 0);
        }
        isRunning_ = false;
        state_ = StreamState::Error;
        return false;
    }

    const auto& rangeStr = rangeIt->second;
    auto startPos = rangeStr.find("bytes ");
    if (startPos == std::string::npos) {
        LogW("[HttpStream::validateRangeResponse] malformed Content-Range: {}", rangeStr);
        lastErrorOriginCode_.store(0, std::memory_order_release);
        if (params_.onError) {
            params_.onError(Error::StreamInvalidData, 0);
        }
        isRunning_ = false;
        state_ = StreamState::Error;
        return false;
    }
    startPos += 6;
    auto dashPos = rangeStr.find('-', startPos);
    if (dashPos == std::string::npos) {
        LogW("[HttpStream::validateRangeResponse] malformed Content-Range: {}", rangeStr);
        lastErrorOriginCode_.store(0, std::memory_order_release);
        if (params_.onError) {
            params_.onError(Error::StreamInvalidData, 0);
        }
        isRunning_ = false;
        state_ = StreamState::Error;
        return false;
    }
    auto startResult =
        StringUtil::fromChars<uint64_t>(rangeStr.substr(startPos, dashPos - startPos));
    if (!startResult || *startResult != readRange_.start()) {
        LogW("[HttpStream::validateRangeResponse] Content-Range start mismatch: got={} expect={}",
             startResult ? *startResult : 0, readRange_.start());
        lastErrorOriginCode_.store(0, std::memory_order_release);
        if (params_.onError) {
            params_.onError(Error::StreamInvalidData, 0);
        }
        isRunning_ = false;
        state_ = StreamState::Error;
        return false;
    }
    return true;
}

void HttpStream::handleData(http::StreamChunk&& chunk) noexcept {
    auto chunkSize = chunk.data->length();
    auto buffer = std::shared_ptr<Data>(chunk.data.release());

    if (params_.onData) {
        params_.onData(DataChunk{
            .data = DataView(buffer->view()),
            .owner = std::move(buffer),
            .offset = committedPosition_,
            .segmentIndex = 0,
            .isLastChunk = chunk.isFinal,
        });
    }

    committedPosition_ += chunkSize;
    position_ = committedPosition_;

    if (committedPosition_ / logProgressBytes() !=
        (committedPosition_ - chunkSize) / logProgressBytes()) {
        LogI("[HttpStream::handleData] progress: pos={} chunk={} url={}", committedPosition_,
             chunkSize, params_.uri);
    }
}

void HttpStream::handleFinal() noexcept {
    isCompleted_ = true;
    state_ = StreamState::Completed;
    isRunning_ = false;
    LogI("[HttpStream::handleFinal] totalReceived={} url={}", committedPosition_, params_.uri);
    if (params_.onComplete) {
        params_.onComplete();
    }
}

StreamInfo HttpStream::info() const noexcept {
    std::lock_guard<std::mutex> lock(infoMutex_);
    return info_;
}

}  // namespace slark
