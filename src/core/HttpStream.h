//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include "HttpRequest.h"
#include "IStream.h"
#include "StringUtil.h"
#include "Task.hpp"
#include "Thread.h"

namespace slark {

class HttpStream : public IStream, public std::enable_shared_from_this<HttpStream> {
 public:
    // ownerThread：Stream 状态唯一归属线程（由 MediaSourceManager 经 StreamFactory 注入）
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
    // 与 Stream 解耦的控制块：网络回调只持有 weak_ptr，close 后 alive=false
    struct StreamControl {
        std::atomic<bool> alive{true};
    };

    // 一次网络操作（一个 HTTP 请求）的完整状态
    struct StreamOperation {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        std::shared_ptr<http::HttpRequest> request;
        Task<void> task;                        // 网络协程（保活 frame）
        std::shared_ptr<StreamControl> control; // weak 回调用
    };

    // 网络协程投递给 owner 线程的不可变事件
    struct StreamEvent {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        http::StreamChunk chunk;
    };

    void onEvent(StreamEvent event) noexcept;  // owner 线程执行
    void handleError(const http::ErrorInfo& error) noexcept;
    void handleHeader(const http::StreamChunk& chunk) noexcept;
    void handleData(http::StreamChunk&& chunk) noexcept;
    void handleFinal() noexcept;

    // C-10：Range 响应校验（owner 线程，header 到达时）
    bool validateRangeResponse(const http::StreamChunk& chunk) noexcept;

    // 4.1.3.1：cancel 后等待 task 到达 final suspend，超时后保留 frame
    void cancelCurrentOperation(uint64_t timeoutMs) noexcept;

    // 假设 mutex_ 已持有：启动新操作（seek 内部复用）
    void startLocked() noexcept;

    // 仅在 owner 线程变更状态
    [[nodiscard]] bool onOwnerThread() const noexcept {
        return ownerThread_ && ownerThread_->getId() == std::this_thread::get_id();
    }

 private:
    std::shared_ptr<Thread> ownerThread_;

    // 保护操作状态（start/pause/seek/close 与 onEvent 可能来自不同线程：
    // start/pause/close 在 owner 线程，seek 在 demuxer 线程，onEvent 在 owner 线程）
    mutable std::mutex mutex_;

    // ---- Owner 线程独占 ----
    std::optional<StreamOperation> operation_;
    uint64_t generation_ = 0;
    uint64_t nextOperationId_ = 1;
    bool closing_ = false;
    uint64_t committedPosition_ = 0;  // 已交付 onData 的位置

    OpenParams params_;
    Range readRange_;
    uint64_t contentLength_ = 0;
    bool acceptRanges_ = false;

    StreamInfo info_;
    mutable std::mutex infoMutex_;  // 保护 info_ 快照（外部线程读）

    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<uint64_t> position_{0};
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> isCompleted_{false};
    std::atomic<bool> isPaused_{false};
    std::atomic<int32_t> lastErrorOriginCode_{0};
};

}  // namespace slark
