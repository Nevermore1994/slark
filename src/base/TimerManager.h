//
// Created by Nevermore on 2021/10/22.
// slark TimerManager
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include "NonCopyable.h"
#include "Timer.h"
#include "TimerPool.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace slark {

/// 全局定时器管理器，负责线程调度和同步
class TimerManager final : public NonCopyMovable {
public:
    static TimerManager& shareInstance();

    ~TimerManager() final;

    TimerId runAt(Time::TimePoint timePoint, TimerTask func) noexcept;

    TimerId runAfter(std::chrono::milliseconds delay, TimerTask func) noexcept;

    TimerId runLoop(std::chrono::milliseconds interval, TimerTask func) noexcept;

    void cancel(TimerId id) noexcept;

    void clear() noexcept;

private:
    TimerManager();

    void tickLoop(std::stop_token stopToken);

    TimerId addTimer(Time::TimePoint expireTime,
                     TimerTask func,
                     bool isLoop,
                     std::chrono::milliseconds interval) noexcept;

private:
    std::mutex mutex_;
    std::condition_variable_any cond_;
    bool hasNewTimer_ = false;
    TimerPool timerPool_;
    std::jthread tickThread_;
};

} // namespace slark
