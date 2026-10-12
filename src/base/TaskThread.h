//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Thread.h"
#include "Timer.h"

namespace slark {

class TaskThread final : public Thread {
 public:
    enum class LaunchMode { Immediate, Deferred };

    static std::shared_ptr<TaskThread> create(std::string name,
                                              LaunchMode mode = LaunchMode::Immediate);

    bool isRunning() const noexcept override;
    void stop() noexcept override;
    void launch() noexcept;

    TimerId postTask(std::chrono::milliseconds delay, ThreadTask task) noexcept;
    TimerId postTask(Time::TimePoint at, ThreadTask task) noexcept;
    TimerId postRepeatTask(std::chrono::milliseconds interval, ThreadTask task) noexcept;
    bool cancel(TimerId id) noexcept;

    using Thread::postTask;

    size_t pendingTimerCount() const noexcept;

    ~TaskThread() noexcept override;

 private:
    enum class OneShotState : uint8_t { Pending, Running, Cancelled };
    enum class RepeatState : uint8_t { Idle, Running, Cancelled };

    struct OneShot {
        TimerId id;
        std::atomic<OneShotState> state{OneShotState::Pending};
        ThreadTask task;
    };

    struct Repeat {
        TimerId id;
        std::chrono::milliseconds interval;
        std::atomic<RepeatState> state{RepeatState::Idle};
        ThreadTask task;
        Time::TimePoint lastRunTime{0};
    };

    TaskThread(std::string name, LaunchMode mode);
    void runLoop() noexcept;
    void runRepeatTasks(std::chrono::milliseconds& waitTime) noexcept;
    TimerId postOneShot(Time::TimePoint at, ThreadTask task) noexcept;
    void cancelOutstanding() noexcept;
    void eraseOneShot(TimerId id) noexcept;

    const LaunchMode mode_;
    std::atomic<bool> launched_{false};
    std::atomic<uint64_t> repeatVersion_{0};
    std::vector<std::shared_ptr<Repeat>> repeatTasks_;
    std::unordered_map<uint64_t, std::shared_ptr<OneShot>> oneShots_;
};

}  // namespace slark
