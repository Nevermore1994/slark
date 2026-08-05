//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <thread>

#include "Error.h"
#include "MPSCQueue.hpp"
#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"
#include "Timer.h"

namespace slark {

using ThreadTask = MoveOnlyFunction<void()>;

class Thread : public NonCopyMovable, public std::enable_shared_from_this<Thread> {
 public:
    [[nodiscard]] static std::shared_ptr<Thread> create(std::string name);

    [[nodiscard]] static std::shared_ptr<Thread> createWithLoop(std::string name,
                                                                std::function<void(Thread&)> loop);

    ~Thread() noexcept;

    void start() noexcept;

    void pause() noexcept;

    void stop() noexcept;

    // 同步等待 worker 线程退出。
    // 必须先调用 stop()/loop 自然退出条件后再 join，否则会阻塞。
    // 若在 worker 自身线程上下文调用则跳过，避免 self-join 死锁。
    void join() noexcept;

    Expected<void> postTask(ThreadTask task) noexcept;
    TimerId postTask(std::chrono::milliseconds delay, ThreadTask task) noexcept;
    TimerId postTask(Time::TimePoint timePoint, ThreadTask task) noexcept;
    TimerId postRepeatTask(std::chrono::milliseconds interval, ThreadTask task) noexcept;
    void cancelRepeatTask(TimerId id) noexcept;

    [[nodiscard]] inline bool isActive() const noexcept {
        return isActive_.load(std::memory_order_acquire);
    }

    class ScopedInactive {
     public:
        ScopedInactive() noexcept;
        ~ScopedInactive() noexcept;
        ScopedInactive(const ScopedInactive&) = delete;
        ScopedInactive& operator=(const ScopedInactive&) = delete;

     private:
        Thread* thread_;
    };

    static void setCurrentActive(bool active) noexcept;

 public:
    [[nodiscard]] inline bool isRunning() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return isRunning_;
    }

    [[nodiscard]] inline bool isExit() const noexcept {
        return stopRequested_.load(std::memory_order_acquire);
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

    [[nodiscard]] bool shouldStop() noexcept;

    void drainPendingWork() noexcept;

 private:
    explicit Thread(std::string name);

    void process() noexcept;

    void setup() noexcept;

    void drainTaskQueue() noexcept;

    void waitIdle(std::chrono::milliseconds waitTime,
                  std::unique_lock<std::mutex>& lock) noexcept;

    std::chrono::milliseconds runRepeatTasks() noexcept;

    TimerTask wrapOneShotTask(ThreadTask task) noexcept;

    TimerId registerTimer(TimerId id) noexcept;

    [[nodiscard]] static Thread* current() noexcept;

    void setActive(bool active) noexcept {
        if (active) {
            lastRunTimeStamp_.store(Time::nowTimeStamp().point());
        }
        isActive_.store(active, std::memory_order_release);
    }

    friend class ScopedInactive;

 private:
    struct RepeatTask {
        TimerId id;
        std::chrono::milliseconds interval;
        std::shared_ptr<ThreadTask> task;
        Time::TimePoint lastRunTime;
    };

    bool isRunning_ = false;
    std::atomic<bool> isInit_ = false;
    std::atomic<bool> stopRequested_ = false;
    std::atomic<bool> isActive_{false};
    std::string name_;
    mutable std::mutex mutex_;
    std::condition_variable_any cond_;
    std::atomic<uint64_t> lastRunTimeStamp_;
    MPSCQueue<ThreadTask> taskQueue_;
    std::vector<TimerId> timerIds_;
    std::vector<RepeatTask> repeatTasks_;
    std::function<void(Thread&)> customWork_;
    std::thread worker_;
};

}  // end namespace slark
