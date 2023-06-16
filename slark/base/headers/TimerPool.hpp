//
// Created by Nevermore on 2021/10/25.
// Slark TimerPool
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>
#include <mutex>
#include "Time.hpp"
#include "Timer.hpp"

namespace Slark {

class TimerPool {
public:
    TimerPool() = default;

    ~TimerPool();

    TimerPool(const TimerPool&) = delete;

    TimerPool& operator=(const TimerPool&) = delete;

    TimerPool(TimerPool&&) noexcept;

    TimerPool& operator=(TimerPool&&) noexcept;

    TimerId runAt(uint64_t timeStamp, TimerCallback func) noexcept;

    TimerId runAfter(uint64_t delayTime, TimerCallback func) noexcept; //ms
    TimerId runLoop(uint64_t timeInterval, TimerCallback func) noexcept;

    TimerId runAfter(std::chrono::milliseconds timeStamp, TimerCallback func) noexcept; //ms
    TimerId runLoop(std::chrono::milliseconds timeStamp, TimerCallback func) noexcept;

    void cancel(TimerId id);

    void loop() noexcept;

    void clear() noexcept;

private:
    TimerId addTimer(uint64_t timeStamp, TimerCallback func, bool isLoop, uint64_t timeInterval = 1000) noexcept;

    void remove(TimerId id) noexcept;

private:
    std::priority_queue<TimerInfo, std::vector<TimerInfo>, decltype(TimerInfoCompare())> timerInfos_;
    std::unordered_map<TimerId, Timer> timers_;
    std::mutex mutex_;
};

}
