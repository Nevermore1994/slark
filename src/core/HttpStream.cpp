//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HttpStream.h"

#include <chrono>
#include <format>
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
    if (rangeCapability_ != RangeCapability::Supported) {
        LogW("[HttpStream::seek] reject, server does not support Range, capability={} pos={}",
             static_cast<int>(rangeCapability_), position);
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

void HttpStream::updateRangeCapability(RangeCapability capability) noexcept {
    rangeCapability_ = capability;
    std::lock_guard<std::mutex> lock(infoMutex_);
    info_.isSeekable = capability == RangeCapability::Supported;
}

void HttpStream::handleHeader(const http::StreamChunk& chunk) noexcept {
    // Range capability is converged in validateRangeResponse (based on protocol behavior);
    // here we only parse metadata and no longer rely on the Accept-Ranges header.
    if (!validateRangeResponse(chunk)) {
        return;
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

    LogI("[HttpStream::handleHeader] status={} rangeCapability={} contentLength={} totalSize={} "
         "url={}",
         static_cast<int>(chunk.statusCode), static_cast<int>(rangeCapability_), contentLength_,
         info_.totalSize, params_.uri);
}

bool HttpStream::failRangeValidation(slark::Error error,
                                     int32_t originCode,
                                     std::string detail) noexcept {
    LogW("[HttpStream::validateRangeResponse] {}", detail);
    lastErrorOriginCode_.store(originCode, std::memory_order_release);
    if (params_.onError) {
        params_.onError(error, originCode);
    }
    isRunning_ = false;
    state_ = StreamState::Error;
    return false;
}

bool HttpStream::validateRangeResponse(const http::StreamChunk& chunk) noexcept {
    const auto kStatus = chunk.statusCode;

    // Request explicitly rejected (416): target is out of the resource range
    if (kStatus == http::HttpStatusCode::RangeNotSatisfiable) {
        return failRangeValidation(
            Error::OutOfRange, 416,
            std::format("416, requested range not satisfiable: {}", readRange_.toString()));
    }

    // Server ignores Range and returns a 200 full body:
    // - Request starting from 0 (or without Range): data is still sequential from 0,
    //   playback can degrade gracefully
    // - Request at a non-zero position: full body starts at 0, misaligned with expectations,
    //   treated as an error
    if (kStatus != http::HttpStatusCode::PartialContent) {
        const bool startFromZero = !readRange_.isValid() || readRange_.start() == 0;
        if (!startFromZero) {
            return failRangeValidation(
                Error::StreamNotReady, static_cast<int32_t>(kStatus),
                std::format("server ignored Range, status={} requested={}",
                            static_cast<int>(kStatus), readRange_.toString()));
        }
        LogW("[HttpStream::validateRangeResponse] server ignored Range, fallback sequential, "
             "status={}",
             static_cast<int>(kStatus));
        updateRangeCapability(RangeCapability::Unsupported);
        return true;
    }

    // 206: the server actually accepted the Range request; capability is based on behavior
    // (some servers omit the Accept-Ranges header in 206 responses)
    updateRangeCapability(RangeCapability::Supported);

    if (!readRange_.isValid()) {
        return true;
    }

    auto rangeIt = chunk.headers.find("content-range");
    if (rangeIt == chunk.headers.end()) {
        return failRangeValidation(Error::StreamInvalidData, 0, "206 without Content-Range");
    }

    const auto& rangeStr = rangeIt->second;
    auto startPos = rangeStr.find("bytes ");
    if (startPos == std::string::npos) {
        return failRangeValidation(
            Error::StreamInvalidData, 0,
            std::format("malformed Content-Range: {}", rangeStr));
    }
    startPos += 6;
    auto dashPos = rangeStr.find('-', startPos);
    if (dashPos == std::string::npos) {
        return failRangeValidation(
            Error::StreamInvalidData, 0,
            std::format("malformed Content-Range: {}", rangeStr));
    }
    auto startResult =
        StringUtil::fromChars<uint64_t>(rangeStr.substr(startPos, dashPos - startPos));
    if (!startResult || *startResult != readRange_.start()) {
        return failRangeValidation(
            Error::StreamInvalidData, 0,
            std::format("Content-Range start mismatch: got={} expect={}",
                        startResult ? *startResult : 0, readRange_.start()));
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
