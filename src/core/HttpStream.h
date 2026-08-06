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

// Server Range capability, inferred from actual response behavior rather than header claims:
//   Unknown      no response received yet, capability unknown
//   Supported    received 206 + matching Content-Range (behavioral proof)
//   Unsupported  received 200 full body (server ignores Range)
enum class RangeCapability : uint8_t { Unknown, Supported, Unsupported };

class HttpStream : public IStream, public std::enable_shared_from_this<HttpStream> {
 public:
    // ownerThread: the single owning thread of Stream state (injected by MediaSourceManager
    // through StreamFactory)
    explicit HttpStream(std::shared_ptr<Thread> ownerThread);
    ~HttpStream() override;

    auto open(const OpenParams& params) -> StreamResult<StreamInfo> override;
    void close() noexcept override;
    void start() noexcept override;
    void pause() noexcept override;
    auto seek(uint64_t position) -> StreamResult<void> override;
    void updateRange(Range range) noexcept override;

    [[nodiscard]] StreamState state() const noexcept override {
        return state_.load();
    }

    [[nodiscard]] StreamInfo info() const noexcept override;

    [[nodiscard]] uint64_t position() const noexcept override {
        return position_.load();
    }

    [[nodiscard]] bool isRunning() const noexcept override {
        return isRunning_.load();
    }

    [[nodiscard]] bool isCompleted() const noexcept override {
        return isCompleted_.load();
    }

    [[nodiscard]] int32_t lastErrorOriginCode() const noexcept {
        return lastErrorOriginCode_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::string host() const noexcept {
        return StringUtil::extractHostFromUri(params_.uri);
    }

 private:
    // Control block decoupled from the Stream: network callbacks hold only a weak_ptr,
    // alive=false after close
    struct StreamControl {
        std::atomic<bool> alive{true};
    };

    // Complete state of one network operation (one HTTP request)
    struct StreamOperation {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        std::shared_ptr<http::HttpRequest> request;
        Task<void> task;                        // network coroutine (keeps the frame alive)
        std::shared_ptr<StreamControl> control; // for weak callbacks
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

    // C-10: Range response validation (owner thread, when the header arrives).
    // Converges the server's Range capability into a single source of truth:
    // 206+Content-Range → Supported, 200 full body → Unsupported (continue sequential
    // transfer), anything else → error.
    bool validateRangeResponse(const http::StreamChunk& chunk) noexcept;

    // Unified Range validation failure path: record the error, report onError, enter Error state
    bool failRangeValidation(slark::Error error, int32_t originCode, std::string detail) noexcept;

    // Synchronously update RangeCapability and info_.isSeekable (owner thread)
    void updateRangeCapability(RangeCapability capability) noexcept;

    // 4.1.3.1: after cancel, wait for the task to reach its final suspend; keep the frame on timeout
    void cancelCurrentOperation(uint64_t timeoutMs) noexcept;

    // Assumes mutex_ is held: start a new operation (reused internally by seek)
    void startLocked() noexcept;

    // State is only mutated on the owner thread
    [[nodiscard]] bool onOwnerThread() const noexcept {
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
    uint64_t nextOperationId_ = 1;
    bool closing_ = false;
    uint64_t committedPosition_ = 0;  // position already delivered to onData

    OpenParams params_;
    Range readRange_;
    uint64_t contentLength_ = 0;
    RangeCapability rangeCapability_{RangeCapability::Unknown};

    StreamInfo info_;
    mutable std::mutex infoMutex_;  // protects the info_ snapshot (read by external threads)

    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<uint64_t> position_{0};
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> isCompleted_{false};
    std::atomic<bool> isPaused_{false};
    std::atomic<int32_t> lastErrorOriginCode_{0};
};

}  // namespace slark
