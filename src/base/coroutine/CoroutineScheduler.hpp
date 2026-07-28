// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <chrono>
#include <coroutine>
#include <functional>
#include <memory>
#include <type_traits>

#include "NonCopyable.h"
#include "Timer.h"
#include "TimerManager.h"

namespace slark {

class CoroutineScheduler : public NonCopyable {
 public:
    static CoroutineScheduler& instance() noexcept {
        static CoroutineScheduler scheduler;
        return scheduler;
    }

    template <std::invocable Func>
    TimerId scheduleCallback(std::chrono::milliseconds delay, Func&& callback) {
        return TimerManager::shareInstance().runAfter(delay, std::forward<Func>(callback));
    }

    void cancel(TimerId timerId) noexcept {
        if (timerId.isValid()) {
            TimerManager::shareInstance().cancel(timerId);
        }
    }

 private:
    CoroutineScheduler() = default;
    ~CoroutineScheduler() = default;
};

}  // namespace slark
