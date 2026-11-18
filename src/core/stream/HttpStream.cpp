//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HttpStream.h"

#include <array>
#include <cassert>
#include <chrono>
#include <deque>
#include <format>
#include <optional>
#include <limits>
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

// Chunks the network coroutine may post ahead of the owner thread (~16KB each)
constexpr size_t kMaxPendingChunks = 16;

struct WindowSlots {
    std::array<std::optional<CompletionAwaiter<void>>, kMaxPendingChunks> slots;
};

constexpr std::chrono::hours kStreamTotalTimeout{24};
constexpr std::chrono::seconds kStreamIdleTimeout{30};
}  // namespace

http::RequestInfo HttpStream::makeRequestInfo(const std::string& uri, const Range& range) {
    http::RequestInfo requestInfo;
    requestInfo.url = uri;
    requestInfo.methodType = http::HttpMethodType::Get;
    requestInfo.readTimeout = kStreamTotalTimeout;
    requestInfo.readIdleTimeout = kStreamIdleTimeout;
    requestInfo.maxResponseBodySize = std::numeric_limits<uint64_t>::max();
    if (range.isValid()) {
        requestInfo.headers["Range"] = range.toHeaderString();
    }
    return requestInfo;
}

HttpStream::HttpStream(std::shared_ptr<TaskThread> ownerThread)
    : ownerThread_(std::move(ownerThread)) {}

HttpStream::~HttpStream() {
    close();
}

auto HttpStream::open(const OpenParams& params) -> StreamResult<StreamInfo> {
    params_ = params;
    readRange_ = params.range;
    deliveredPosition_ = readRange_.isValid() ? readRange_.start() : 0;
    operationCoversSource_ = false;
    sourceGeneration_ = 0;

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
    std::unique_lock<std::mutex> lock(mutex_);
    closing_ = true;
    ++generation_;
    cancelCurrentOperation(kCloseWaitTimeout.count());
    state_ = StreamState::Idle;
    isCompleted_ = false;
    isPaused_ = false;
}

void HttpStream::start() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
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

    readRange_ = Range(deliveredPosition_);
    position_ = deliveredPosition_;
    responseContentLength_ = 0;
    operationCoversSource_ = false;
    state_ = StreamState::Reading;
    isPaused_ = false;

    auto request = std::make_shared<http::HttpRequest>();
    auto control = std::make_shared<StreamControl>();
    uint64_t gen = generation_;
    uint64_t opId = nextOperationId_++;

    auto requestInfo = makeRequestInfo(params_.uri, readRange_);

    LogI("[HttpStream::startLocked] url={} range={} gen={} method=GET", requestInfo.url,
         requestInfo.headers.count("Range") ? requestInfo.headers["Range"] : std::string{}, gen);

    std::weak_ptr<HttpStream> weakThis = shared_from_this();
    std::weak_ptr<StreamControl> weakControl = control;

    auto streamTask = [](std::shared_ptr<http::HttpRequest> req, http::RequestInfo info,
                         std::weak_ptr<HttpStream> weakStream,
                         std::weak_ptr<StreamControl> weakControl,
                         std::shared_ptr<TaskThread> ownerThread, uint64_t generation,
                         uint64_t operationId) -> Task<void> {
        auto gen = req->streamingExecute(info);
        auto slots = std::make_shared<WindowSlots>();
        std::deque<std::shared_ptr<CompletionAwaiter<void>>> inFlight;
        size_t nextSlot = 0;
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

            auto& slot = slots->slots[nextSlot];
            if (slot.has_value()) {
                assert(slot->isCompleted());
                slot->reset();
            } else {
                slot.emplace();
            }
            auto delivered = std::shared_ptr<CompletionAwaiter<void>>(slots, &slot.value());
            nextSlot = (nextSlot + 1) % kMaxPendingChunks;
            auto event = HttpStream::StreamEvent{
                generation, operationId, std::move(*chunk)};
            if (!ownerThread->postTask(
                    [weakStream, delivered, event = std::move(event)]() mutable {
                        if (auto s = weakStream.lock()) {
                            s->onEvent(std::move(event));
                        }
                        delivered->complete();
                    })) {
                co_return;
            }

            // A per-chunk round trip to the owner thread caps throughput; only block once
            // the window is full
            inFlight.push_back(std::move(delivered));
            while (!inFlight.empty() && inFlight.front()->isCompleted()) {
                inFlight.pop_front();
            }
            while (inFlight.size() >= kMaxPendingChunks) {
                co_await *inFlight.front();
                inFlight.pop_front();
            }
        }
    };

    auto task = streamTask(request, requestInfo, weakThis, weakControl, ownerThread_, gen, opId);
    operation_.emplace(StreamOperation{gen, opId, request, coro::makeJob(std::move(task)),
                                       std::move(control)});
    operation_->job.start();
}

void HttpStream::pause() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (state_ != StreamState::Reading) {
        LogI("[HttpStream::pause] noop, state={}", static_cast<int>(state_.load()));
        return;
    }

    ++generation_;
    cancelCurrentOperation(kWaitSettleTimeout.count());

    state_ = StreamState::Paused;
    LogI("[HttpStream::pause] deliveredPos={}", deliveredPosition_);
}

auto HttpStream::seek(uint64_t position, uint64_t sourceGeneration) -> StreamResult<void> {
    std::unique_lock<std::mutex> lock(mutex_);
    if (rangeCapability_ != RangeCapability::Supported) {
        LogW("[HttpStream::seek] reject, server does not support Range, capability={} pos={}",
             static_cast<int>(rangeCapability_), position);
        return std::unexpected(Error::StreamNotReady);
    }

    ++generation_;
    cancelCurrentOperation(kWaitSettleTimeout.count());

    deliveredPosition_ = position;
    sourceGeneration_ = sourceGeneration;
    isCompleted_ = false;
    state_ = StreamState::Ready;
    LogI("[HttpStream::seek] pos={} url={}", position, params_.uri);
    startLocked();
    return {};
}

void HttpStream::setReadRange(Range range) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    readRange_ = range;
}

void HttpStream::closeFetchGate() noexcept {
    pause();
}

void HttpStream::openFetchGate() noexcept {
    start();
}

void HttpStream::cancelCurrentOperation(uint64_t timeoutMs) noexcept {
    if (!operation_) {
        return;
    }
    operation_->request->cancel();
    operation_->control->alive = false;

    auto job = std::move(operation_->job);
    operation_.reset();
    job.cancel();
    mutex_.unlock();

    if (!job.waitFor(std::chrono::milliseconds(timeoutMs))) {
        LogW("[HttpStream::cancelCurrentOperation] task not settled within {}ms", timeoutMs);
    }
    mutex_.lock();
}

void HttpStream::onEvent(StreamEvent event) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (event.generation != generation_ || !operation_ ||
        event.operationId != operation_->operationId) {
        return;
    }
    // Events already in flight when the response was rejected must not report again
    if (state_ == StreamState::Error) {
        return;
    }
    auto& chunk = event.chunk;

    if (chunk.hasError()) {
        LogE("[HttpStream::onEvent] error chunk: retCode={} originCode={} pos={} url={}",
             static_cast<int>(chunk.error.retCode), chunk.error.originErrorCode, position_.load(),
             params_.uri);
        if (chunk.error.retCode == Error::Cancelled) {
            return;
        }
        handleError(chunk.error);
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
    LogE("[HttpStream::handleError] retCode={} originCode={} pos={} url={}",
         toString(error.retCode), error.originErrorCode, position_.load(), params_.uri);
    if (params_.onError) {
        params_.onError(error.retCode, error.originErrorCode);
    }
    state_ = StreamState::Error;
}

void HttpStream::updateRangeCapability(RangeCapability capability) noexcept {
    rangeCapability_ = capability;
    std::lock_guard<std::mutex> lock(infoMutex_);
    info_.isSeekable = capability == RangeCapability::Supported;
}

void HttpStream::handleHeader(const http::StreamChunk& chunk) noexcept {
    if (!validateResponse(chunk)) {
        return;
    }

    auto lengthIt = chunk.headers.find("content-length");
    if (lengthIt != chunk.headers.end()) {
        auto lenResult = StringUtil::fromChars<uint64_t>(lengthIt->second);
        if (lenResult) {
            responseContentLength_ = *lenResult;
            std::lock_guard<std::mutex> lock(infoMutex_);
            info_.totalSize = responseContentLength_;
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

    uint64_t totalSize = 0;
    {
        std::lock_guard<std::mutex> lock(infoMutex_);
        totalSize = info_.totalSize;
    }
    operationCoversSource_ = readRange_.start() == 0 && totalSize > 0 &&
                             responseContentLength_ == totalSize;

    LogI("[HttpStream::handleHeader] status={} rangeCapability={} responseContentLength={} "
         "totalSize={} coversSource={} url={}",
         static_cast<int>(chunk.statusCode), static_cast<int>(rangeCapability_),
         responseContentLength_, totalSize, operationCoversSource_, params_.uri);
}

bool HttpStream::failResponse(slark::Error error,
                              int32_t originCode,
                              std::string detail) noexcept {
    LogW("[HttpStream::validateResponse] {}", detail);
    if (params_.onError) {
        params_.onError(error, originCode);
    }
    state_ = StreamState::Error;
    // Keep operation_ so close() still settles the task; only the transfer is stopped
    if (operation_) {
        operation_->request->cancel();
        operation_->control->alive = false;
    }
    return false;
}

bool HttpStream::validateResponse(const http::StreamChunk& chunk) noexcept {
    const auto kStatus = chunk.statusCode;

    if (kStatus == http::HttpStatusCode::RangeNotSatisfiable) {
        return failResponse(
            Error::OutOfRange, 416,
            std::format("416, requested range not satisfiable: {}", readRange_.toString()));
    }

    const auto code = static_cast<int32_t>(kStatus);
    if (code < 200 || code >= 300) {
        return failResponse(Error::HttpError, code,
                            std::format("unexpected status {}, url={}", code, params_.uri));
    }

    if (kStatus != http::HttpStatusCode::PartialContent) {
        const bool startFromZero = !readRange_.isValid() || readRange_.start() == 0;
        if (!startFromZero) {
            return failResponse(
                Error::StreamNotReady, static_cast<int32_t>(kStatus),
                std::format("server ignored Range, status={} requested={}",
                            static_cast<int>(kStatus), readRange_.toString()));
        }
        LogW("[HttpStream::validateResponse] server ignored Range, fallback sequential, "
             "status={}",
             static_cast<int>(kStatus));
        updateRangeCapability(RangeCapability::Unsupported);
        return true;
    }

    updateRangeCapability(RangeCapability::Supported);

    if (!readRange_.isValid()) {
        return true;
    }

    auto rangeIt = chunk.headers.find("content-range");
    if (rangeIt == chunk.headers.end()) {
        return failResponse(Error::StreamInvalidData, 0, "206 without Content-Range");
    }

    const auto& rangeStr = rangeIt->second;
    auto startPos = rangeStr.find("bytes ");
    if (startPos == std::string::npos) {
        return failResponse(
            Error::StreamInvalidData, 0,
            std::format("malformed Content-Range: {}", rangeStr));
    }
    startPos += 6;
    auto dashPos = rangeStr.find('-', startPos);
    if (dashPos == std::string::npos) {
        return failResponse(
            Error::StreamInvalidData, 0,
            std::format("malformed Content-Range: {}", rangeStr));
    }
    auto startResult =
        StringUtil::fromChars<uint64_t>(rangeStr.substr(startPos, dashPos - startPos));
    if (!startResult || *startResult != readRange_.start()) {
        return failResponse(
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
            .offset = deliveredPosition_,
            .segmentIndex = 0,
            .sourceGeneration = sourceGeneration_,
            .isLastChunk = chunk.isFinal,
        });
    }

    deliveredPosition_ += chunkSize;
    position_ = deliveredPosition_;

    if (deliveredPosition_ / logProgressBytes() !=
        (deliveredPosition_ - chunkSize) / logProgressBytes()) {
        LogI("[HttpStream::handleData] progress: pos={} chunk={} url={}", deliveredPosition_,
             chunkSize, params_.uri);
    }
}

void HttpStream::handleFinal() noexcept {
    isCompleted_ = operationCoversSource_;
    state_ = StreamState::Completed;
    LogI("[HttpStream::handleFinal] totalReceived={} coversSource={} url={}", deliveredPosition_,
         operationCoversSource_, params_.uri);
    if (operationCoversSource_ && params_.onComplete) {
        params_.onComplete(sourceGeneration_);
    }
}

StreamInfo HttpStream::info() const noexcept {
    std::lock_guard<std::mutex> lock(infoMutex_);
    return info_;
}

}  // namespace slark
