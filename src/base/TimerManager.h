//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "NonCopyable.h"
#include "Timer.h"
#include "TimerPool.h"

namespace slark {

/// Global timer manager responsible for thread scheduling and synchronization
class TimerManager final : public NonCopyMovable {
 public:
    static TimerManager& shareInstance();

    ~TimerManager();

    TimerId runAt(Time::TimePoint timePoint, TimerTask func) noexcept;

    TimerId runAfter(std::chrono::milliseconds delay, TimerTask func) noexcept;

    TimerId runLoop(std::chrono::milliseconds interval, TimerTask func) noexcept;

    void cancel(TimerId id) noexcept;

    void clear() noexcept;

 private:
    TimerManager();

    void tickLoop();

    void processLoopTask(TimerEntry& entry);

    TimerId addTimer(Time::TimePoint expireTime,
                     TimerTask func,
                     bool isLoop,
                     std::chrono::milliseconds interval) noexcept;

 private:
    // Wraps a loop task, supports move-only callables
    struct LoopTaskWrapper {
        TimerTask task;

        void operator()() const {
            if (task) {
                task();
            }
        }
    };

    std::mutex mutex_;
    std::condition_variable cond_;
    bool hasNewTimer_ = false;
    TimerPool timerPool_;
    std::thread tickThread_;
    std::atomic<bool> stopRequested_{false};
    // Loop task storage: shared_ptr allows multiple executions
    std::unordered_map<uint64_t, std::shared_ptr<LoopTaskWrapper>> loopTasks_;
};

}  // namespace slark
