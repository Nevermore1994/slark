//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NonCopyable.h"

#include <atomic>
#include <coroutine>
#include <mutex>

namespace slark {

// 协程 frame 的唯一所有者；所有跨异步边界的恢复令牌都必须持有该控制块。
class CoroutineControlBlock {
public:
    explicit CoroutineControlBlock(std::coroutine_handle<> handle) noexcept : handle_(handle) {}

    CoroutineControlBlock(const CoroutineControlBlock&) = delete;
    CoroutineControlBlock& operator=(const CoroutineControlBlock&) = delete;

    ~CoroutineControlBlock() {
        if (handle_) {
            handle_.destroy();
        }
    }

    [[nodiscard]] std::coroutine_handle<> handle() const noexcept { return handle_; }

    [[nodiscard]] bool isDone() const noexcept {
        return completed_.load(std::memory_order_acquire) || (handle_ && handle_.done());
    }

    void markCompleted() noexcept { completed_.store(true, std::memory_order_release); }

    void resume() noexcept {
        if (handle_ && !isDone()) {
            handle_.resume();
        }
    }

    void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }

    [[nodiscard]] bool isCancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

private:
    std::coroutine_handle<> handle_ = nullptr;
    std::atomic<bool> completed_{false};
    std::atomic<bool> cancelled_{false};
    std::mutex resumeMutex_;
};

} // namespace slark
