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
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

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

    void processLoopTask(TimerEntry& entry);

    TimerId addTimer(Time::TimePoint expireTime,
                     TimerTask func,
                     bool isLoop,
                     std::chrono::milliseconds interval) noexcept;

private:
    // 循环任务包装器，支持 move-only callable
    struct LoopTaskWrapper {
        TimerTask task;

        void operator()() const {
            if (task) {
                task();
            }
        }
    };

    std::mutex mutex_;
    std::condition_variable_any cond_;
    bool hasNewTimer_ = false;
    TimerPool timerPool_;
    std::jthread tickThread_;
    // 循环任务存储：shared_ptr 支持多次执行
    std::unordered_map<uint64_t, std::shared_ptr<LoopTaskWrapper>> loopTasks_;
};

} // namespace slark
