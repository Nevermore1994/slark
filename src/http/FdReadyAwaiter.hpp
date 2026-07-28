//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
// Phase 4D: Task-compatible FdReadyAwaiter（从 promise 读取 EventLoop）
//

#pragma once

#include <atomic>
#include <memory>

#include "base/coroutine/CancellationToken.h"
#include "base/coroutine/EventLoop.h"
#include "base/coroutine/Task.hpp"

namespace slark::coro {

class FdReadyAwaiterV2 {
 public:
    enum class Interest : uint8_t { Read, Write };

    // loop 显式指定时覆盖 promise 传播的；为 nullptr 走三级回退
    explicit FdReadyAwaiterV2(int32_t fd,
                              Interest interest,
                              slark::EventLoop* loop = nullptr) noexcept
        : fd_(fd), interest_(interest), explicitLoop_(loop) {}

    ~FdReadyAwaiterV2() {
        if (aliveFlag_) {
            *aliveFlag_ = false;
            aliveFlag_.reset();
        }
        unregisterFd();
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        handle_ = h;
        aliveFlag_ = std::make_shared<std::atomic<bool>>(true);

        eventLoop_ = resolveLoop(h.promise());
        if (!eventLoop_) {
            return false;
        }

        auto fdInterest =
            (interest_ == Interest::Read) ? slark::FdInterest::Read : slark::FdInterest::Write;
        auto token = eventLoop_->registerContinuation({
            fd_,
            fdInterest,
            handle_,
            aliveFlag_,
            0 /* generation */
        });
        registered_ = true;

        if (interest_ == Interest::Read) {
            eventLoop_->setReadable(fd_, true);
        } else {
            eventLoop_->setWritable(fd_, true);
        }

        // CancellationToken 保护：aliveFlag 抑制析构后回调
        if constexpr (requires { h.promise().executor_; }) {
            // CancellationToken not auto-propagated; use co_withCancellation instead
        }

        return true;
    }

    void await_resume() noexcept {
        if (aliveFlag_) {
            *aliveFlag_ = false;
            aliveFlag_.reset();
        }
    }

 private:
    // EventLoop 三级回退：显式 > promise 传播 > 全局默认
    template <typename Promise>
    slark::EventLoop* resolveLoop(const Promise& promise) noexcept {
        if (explicitLoop_) {
            return explicitLoop_;
        }
        if (promise.eventLoop_) {
            return promise.eventLoop_;
        }
        return slark::EventLoop::getDefault();
    }

    void unregisterFd() noexcept {
        if (registered_ && eventLoop_) {
            auto fdInterest =
                (interest_ == Interest::Read) ? slark::FdInterest::Read : slark::FdInterest::Write;
            eventLoop_->unregisterContinuation(fd_, fdInterest);
            registered_ = false;
        }
    }

    int32_t fd_;
    Interest interest_;
    slark::EventLoop* explicitLoop_ = nullptr;
    slark::EventLoop* eventLoop_ = nullptr;
    bool registered_ = false;
    std::coroutine_handle<> handle_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_;
};

}  // namespace slark::coro
