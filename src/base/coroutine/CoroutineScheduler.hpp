//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "NonCopyable.h"
#include "SharedCoroutineHandle.hpp"
#include "ThreadPool.hpp"
#include "TimerManager.h"

#include <atomic>
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

    void schedule(SharedCoroutineHandle handle) {
        if (!handle.done()) {
            auto shared = std::make_shared<SharedCoroutineHandle>(std::move(handle));
            threadPool_.postJob([shared]() {
                shared->tryResume();
            });
        }
    }

    void schedule(std::shared_ptr<SharedCoroutineHandle> shared) {
        if (shared && !shared->done()) {
            threadPool_.postJob([shared]() {
                shared->tryResume();
            });
        }
    }

    TimerId scheduleAfter(std::chrono::milliseconds delay, SharedCoroutineHandle handle) {
        if (!handle.done()) {
            auto shared = std::make_shared<SharedCoroutineHandle>(std::move(handle));
            return TimerManager::shareInstance().runAfter(delay, [shared]() {
                CoroutineScheduler::instance().schedule(std::move(shared));
            });
        }
        return TimerId::kInvalidTimerId;
    }

    TimerId scheduleAfter(std::chrono::milliseconds delay,
                          std::shared_ptr<SharedCoroutineHandle> shared) {
        if (shared && !shared->done()) {
            return TimerManager::shareInstance().runAfter(delay, [shared]() {
                CoroutineScheduler::instance().schedule(std::move(shared));
            });
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

    void waitForAll() noexcept { threadPool_.waitForAll(); }

    void shutdown() noexcept { threadPool_.clear(); }

private:
    CoroutineScheduler() : threadPool_(ThreadPoolConfig{4}) {}

    ~CoroutineScheduler() {
        threadPool_.waitForAll();
        threadPool_.clear();
    }

    ThreadPool threadPool_;
};

} // namespace slark
