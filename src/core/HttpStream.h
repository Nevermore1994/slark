//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include "Awaitable.hpp"
#include "HttpRequest.h"
#include "IStream.h"
#include "StringUtil.h"
#include "Task.hpp"
#include "Thread.h"

namespace slark {

enum class RangeCapability : uint8_t { Unknown, Supported, Unsupported };

class HttpStream : public IStream, public std::enable_shared_from_this<HttpStream> {
 public:
    explicit HttpStream(std::shared_ptr<Thread> ownerThread);
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

 private:
    // Decoupled from the Stream: network callbacks hold only a weak_ptr, alive=false after close
    struct StreamControl {
        std::atomic<bool> alive{true};
    };

    struct StreamOperation {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        std::shared_ptr<http::HttpRequest> request;
        Task<void> task;  // network coroutine (keeps the frame alive)
        std::shared_ptr<StreamControl> control;
    };

    // Immutable event posted by the network coroutine to the owner thread
    struct StreamEvent {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        http::StreamChunk chunk;
        std::shared_ptr<CompletionAwaiter<void>> delivered;
    };

    void onEvent(StreamEvent event) noexcept;  // runs on the owner thread
    void handleError(const http::ErrorInfo& error) noexcept;
    void handleHeader(const http::StreamChunk& chunk) noexcept;
    void handleData(http::StreamChunk&& chunk) noexcept;
    void handleFinal() noexcept;

    bool validateRangeResponse(const http::StreamChunk& chunk) noexcept;

    bool failRangeValidation(slark::Error error, int32_t originCode, std::string detail) noexcept;

    void updateRangeCapability(RangeCapability capability) noexcept;

    // Cancels and waits for the task to settle; on timeout the frame is left alive on purpose
    void cancelCurrentOperation(uint64_t timeoutMs) noexcept;

    // Requires mutex_ to be held (reused by seek)
    void startLocked() noexcept;

    // State is only mutated on the owner thread
    [[nodiscard]] bool isOnOwnerThread() const noexcept {
        return ownerThread_ && ownerThread_->getId() == std::this_thread::get_id();
    }

 private:
    std::shared_ptr<Thread> ownerThread_;

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

    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<uint64_t> position_{0};
    std::atomic<bool> isCompleted_{false};
    std::atomic<bool> isPaused_{false};
};

}  // namespace slark
