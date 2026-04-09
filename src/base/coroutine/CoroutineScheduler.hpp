//
// Created by Nevermore on 2025/4/1.
// Slark CoroutineScheduler
// Copyright (c) 2025 Nevermore All rights reserved.
//
#pragma once

#include "Awaitable.hpp"
#include "Coroutine.hpp"
#include "ThreadPool.hpp"
#include "TimerManager.h"

#include <chrono>
#include <coroutine>
#include <functional>
#include <memory>

namespace slark {

class CoroutineScheduler : public NonCopyable {
public:
    static CoroutineScheduler& instance() noexcept {
        static CoroutineScheduler scheduler;
        return scheduler;
    }

    void schedule(std::coroutine_handle<> handle) {
        if (handle && !handle.done()) {
            threadPool_.postJob([handle]() {
                if (handle && !handle.done()) {
                    handle.resume();
                }
            });
        }
    }

    template <typename T>
    void schedule(Coroutine<T>& coroutine) {
        if (coroutine.isValid() && !coroutine.isDone()) {
            schedule(coroutine.getHandle());
        }
    }

    TimerId scheduleAfter(std::chrono::milliseconds delay, std::coroutine_handle<> handle) {
        return TimerManager::shareInstance().runAfter(delay, [this, handle]() {
            schedule(handle);
        });
    }

    template <typename T>
    TimerId scheduleAfter(std::chrono::milliseconds delay, Coroutine<T>& coroutine) {
        if (coroutine.isValid()) {
            return scheduleAfter(delay, coroutine.getHandle());
        }
        return TimerId::kInvalidTimerId;
    }

    void cancel(TimerId timerId) noexcept {
        if (!timerId.isValid()) {
            return;
        }
        TimerManager::shareInstance().cancel(timerId);
    }

    template <std::invocable Func>
    TimerId scheduleCallback(std::chrono::milliseconds delay, Func&& callback) {
        return TimerManager::shareInstance().runAfter(delay, std::forward<Func>(callback));
    }

    void waitForAll() noexcept {
        threadPool_.waitForAll();
    }

    void shutdown() noexcept {
        threadPool_.clear();
    }

private:
    CoroutineScheduler() : threadPool_(ThreadPoolConfig{4}) {}

    ~CoroutineScheduler() {
        threadPool_.clear();
    }

    ThreadPool threadPool_;
};

// TimerAwaiter 实现（依赖 CoroutineScheduler）
inline void TimerAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
    CoroutineScheduler::instance().scheduleAfter(duration_, handle);
}

} // namespace slark
