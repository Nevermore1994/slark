//
// Created by Nevermore on 2021/10/25.
// slark TimerPool
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <chrono>
#include <memory>
#include <queue>
#include <unordered_map>
#include <mutex>
#include "Time.hpp"
#include "Timer.h"

namespace slark {

class TimerPool {
public:
    TimerPool() = default;

    ~TimerPool();

    TimerPool(const TimerPool&) = delete;

    TimerPool& operator=(const TimerPool&) = delete;

    TimerPool(TimerPool&&) noexcept;

    TimerPool& operator=(TimerPool&&) noexcept;

    TimerId runAt(Time::TimePoint timePoint, TimerTask func) noexcept;

    TimerId runAfter(std::chrono::milliseconds ms, TimerTask func) noexcept;
    TimerId runLoop(std::chrono::milliseconds interval, TimerTask func) noexcept;

    void cancel(TimerId id) noexcept;

    void cancel(const std::vector<TimerId>& timers) noexcept;

    void loop() noexcept;

    void clear() noexcept;

private:
    TimerId addTimer(Time::TimePoint timePoint, TimerTask func, bool isLoop, std::chrono::milliseconds timeInterval = {}) noexcept;

    void remove(TimerId id) noexcept;

private:
    std::priority_queue<TimerInfo, std::vector<TimerInfo>, decltype(TimerInfoCompare())> timerInfos_;
    std::unordered_map<TimerId, Timer> timers_;
    std::mutex mutex_;
};

}
