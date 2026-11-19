//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "Awaitable.hpp"
#include "HttpRequest.h"
#include "IStream.h"
#include "Job.hpp"
#include "StringUtil.h"
#include "Task.hpp"
#include "TaskThread.h"

namespace slark {

enum class RangeCapability : uint8_t { Unknown, Supported, Unsupported };

class HttpStream : public IStream, public std::enable_shared_from_this<HttpStream> {
 public:
    explicit HttpStream(std::shared_ptr<TaskThread> ownerThread);
    ~HttpStream() override;

    auto open(const OpenParams& params) -> StreamResult<StreamInfo> override;
    void close() noexcept override;
    void start() noexcept override;
    void pause() noexcept override;
    auto seek(uint64_t position, uint64_t sourceGeneration) -> StreamResult<void> override;
    void setReadRange(Range range) noexcept override;

    // A single Range request has no production unit boundary, so the gate cannot be pulled:
    // closing it tears the operation down and opening it resumes from deliveredPosition_.
    void closeFetchGate() noexcept override;
    void openFetchGate() noexcept override;

    [[nodiscard]] StreamState state() const noexcept override {
        return state_.load();
    }

    [[nodiscard]] StreamInfo info() const noexcept override;

    [[nodiscard]] uint64_t position() const noexcept override {
        return position_.load();
    }

    [[nodiscard]] bool isCompleted() const noexcept override {
        return isCompleted_.load();
    }

    [[nodiscard]] std::string host() const noexcept {
        return StringUtil::extractHostFromUri(params_.uri);
    }

    [[nodiscard]] DebugInfo debugInfo() const override;

    // The overall deadline is a wall clock that also runs during backpressure, and a media
    // file has no meaningful size cap, so only the idle timeout bounds a stalled transfer.
    static http::RequestInfo makeRequestInfo(const std::string& uri, const Range& range);

 private:
    // Decoupled from the Stream: network callbacks hold only a weak_ptr, alive=false after close
    struct StreamControl {
        std::atomic<bool> alive{true};
    };

    struct StreamOperation {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        std::shared_ptr<http::HttpRequest> request;
        coro::Job<void> job;  // network coroutine; cancelled and released with the operation
        std::shared_ptr<StreamControl> control;
    };

    // Immutable event posted by the network coroutine to the owner thread
    struct StreamEvent {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        http::StreamChunk chunk;
    };

    void onEvent(StreamEvent event) noexcept;  // runs on the owner thread
    void handleError(const http::ErrorInfo& error) noexcept;
    void handleHeader(const http::StreamChunk& chunk) noexcept;
    void handleData(http::StreamChunk&& chunk) noexcept;
    void handleFinal() noexcept;

    bool validateResponse(const http::StreamChunk& chunk) noexcept;
    void refreshNetDebug(const http::StreamChunk& chunk, uint64_t generation) noexcept;

    // Reports the error, enters Error state and stops the download. Requires mutex_ held.
    bool failResponse(slark::Error error, int32_t originCode, std::string detail) noexcept;

    void updateRangeCapability(RangeCapability capability) noexcept;

    // Cancels the network coroutine and waits at most timeoutMs for it to settle
    void cancelCurrentOperation(uint64_t timeoutMs) noexcept;

    // Requires mutex_ to be held (reused by seek)
    void startLocked() noexcept;

    // State is only mutated on the owner thread
    [[nodiscard]] bool isOnOwnerThread() const noexcept {
        return ownerThread_ && ownerThread_->isCurrentThread();
    }

 private:
    std::shared_ptr<TaskThread> ownerThread_;

    // Protects operation state (start/pause/seek/close and onEvent may come from different
    // threads: start/pause/close on the owner thread, seek on the demuxer thread,
    // onEvent on the owner thread)
    mutable std::mutex mutex_;

    // ---- Owner thread exclusive ----
    std::optional<StreamOperation> operation_;
    uint64_t generation_ = 0;
    uint64_t sourceGeneration_ = 0;
    uint64_t nextOperationId_ = 1;
    bool closing_ = false;
    uint64_t deliveredPosition_ = 0;
    bool operationCoversSource_ = false;

    OpenParams params_;
    Range readRange_;
    uint64_t responseContentLength_ = 0;
    RangeCapability rangeCapability_{RangeCapability::Unknown};

    StreamInfo info_;
    mutable std::mutex infoMutex_;  // protects the info_ snapshot (read by external threads)

    struct NetDebug {
        uint64_t positionBytes = 0;
        uint64_t totalBytes = 0;
        uint64_t receivedBytes = 0;
        int statusCode = 0;
        std::string reasonPhrase;
        std::string contentType;
        std::string rangeCapability;
        std::string streamState;
        double connectMs = 0.0;
        double tlsHandshakeMs = 0.0;
        double totalRequestMs = 0.0;
        int64_t headerParseUs = 0;
        int32_t chunkCount = 0;
        int32_t redirectCount = 0;
        int32_t recvLoops = 0;
        int32_t sendRetryCount = 0;
        int32_t receiveWaitCount = 0;
        std::string timeoutSource;
        uint64_t connectionGeneration = 0;
        bool connectionReused = false;
        bool connectionBusy = false;
    };

    // Written on the owner thread, read by debugLines() from any thread.
    mutable std::mutex debugMutex_;
    NetDebug netDebug_;

    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<uint64_t> position_{0};
    std::atomic<bool> isCompleted_{false};
    std::atomic<bool> isPaused_{false};
};

}  // namespace slark
