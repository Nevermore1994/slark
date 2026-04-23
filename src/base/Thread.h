//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Error.h"
#include "MPSCQueue.hpp"
#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"
#include "Timer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <thread>

namespace slark {

using Task = MoveOnlyFunction<void()>;

class Thread : public NonCopyMovable, public std::enable_shared_from_this<Thread> {
public:
    [[nodiscard]] static std::shared_ptr<Thread> create(std::string name);

    ~Thread() noexcept override;

    void start() noexcept;

    void pause() noexcept;

    void stop() noexcept;

    /// 立即投递任务到线程队列
    Expected<void> schedule(Task task) noexcept;

    /// 延迟投递一次性任务
    TimerId schedule(std::chrono::milliseconds delay, Task task) noexcept;

    /// 指定时间点投递一次性任务
    TimerId schedule(Time::TimePoint timePoint, Task task) noexcept;

    /// 循环执行任务（执行完后再等待 interval，非固定间隔触发）
    TimerId scheduleRepeat(std::chrono::milliseconds interval, Task task) noexcept;

    /// 取消定时任务
    void cancelSchedule(TimerId id) noexcept;

public:
    [[nodiscard]] inline bool isRunning() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return isRunning_;
    }

    [[nodiscard]] inline bool isExit() const noexcept {
        return worker_.get_stop_token().stop_requested();
    }

    [[nodiscard]] inline Time::TimePoint getLastRunTimeStamp() const noexcept {
        return lastRunTimeStamp_.load();
    }

    [[nodiscard]] inline std::thread::id getId() const noexcept {
        return worker_.get_id();
    }

    [[nodiscard]] inline uint32_t getHashId() const noexcept {
        return static_cast<uint32_t>(std::hash<std::thread::id>{}(worker_.get_id()));
    }

    [[nodiscard]] inline std::string getName() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return name_;
    }

    void setThreadName(std::string_view nameView) noexcept;

private:
    explicit Thread(std::string name);

    void process(std::stop_token stopToken) noexcept;

    void setup() noexcept;

    void drainTaskQueue() noexcept;

    void runRepeatTasks() noexcept;

    [[nodiscard]] bool checkRepeatTaskDue() noexcept;

    TimerTask wrapOneShotTask(Task task) noexcept;

    TimerId registerTimer(TimerId id) noexcept;

private:
    struct RepeatTask {
        TimerId id;
        std::chrono::milliseconds interval;
        std::shared_ptr<Task> task; // 使用 shared_ptr 避免悬空指针
        Time::TimePoint lastRunTime;
    };

    bool isRunning_ = false;
    std::atomic<bool> isInit_ = false;
    std::string name_;
    mutable std::mutex mutex_;
    std::condition_variable_any cond_;
    std::atomic<uint64_t> lastRunTimeStamp_;
    MPSCQueue<Task> taskQueue_;
    std::vector<TimerId> timerIds_; // guarded by mutex_
    std::vector<RepeatTask> repeatTasks_;
    std::jthread worker_;
};

} // end namespace slark
