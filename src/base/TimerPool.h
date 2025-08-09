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
#include "NonCopyable.h"
#include "Timer.h"

namespace slark {

class TimerPool: public NonCopyable {
public:
    TimerPool() = default;

    ~TimerPool() override;

    TimerId runAt(Time::TimePoint timePoint, TimerTask func, ExecuteMode mode) noexcept;

    TimerId runAfter(std::chrono::milliseconds ms, TimerTask func, ExecuteMode mode) noexcept;

    TimerId runLoop(std::chrono::milliseconds interval, TimerTask func, ExecuteMode mode) noexcept;

    void cancel(TimerId id) noexcept;

    void cancel(const std::vector<TimerId>& timers) noexcept;

    ///execution of the current thread
    void loop() noexcept;

    ///the caller is responsible for handling the task
    void loop(std::function<void(ExecuteMode, TimerTask&&)>&& doTask) noexcept;

    void clear() noexcept;

    bool empty() noexcept;
    
    std::chrono::milliseconds peekActiveTime() const noexcept;
private:
    TimerId addTimer(Time::TimePoint timePoint,
                     TimerTask func,
                     bool isLoop,
                     std::chrono::milliseconds timeInterval = {},
                     ExecuteMode mode = ExecuteMode::Serial) noexcept;

private:
    bool isExited_ = false;
    std::priority_queue<TimerInfo, std::vector<TimerInfo>, decltype(TimerInfoCompare())> timerInfos_;
    std::unordered_map<TimerId, Timer, TimerIdHasher, TimerIdEqual> timers_;
    mutable std::mutex mutex_;
};

}
