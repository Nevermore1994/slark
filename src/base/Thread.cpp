//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <functional>
#include <thread>

#ifndef __APPLE__
#include <pthread.h>
#endif

#include "Assert.hpp"
#include "Thread.h"
#include "TimerManager.h"

namespace slark {
using std::chrono::milliseconds;
using namespace std::chrono_literals;

std::shared_ptr<Thread> Thread::create(std::string name) {
    auto thread = std::shared_ptr<Thread>(new Thread(std::move(name)));
    // 使用 shared_from_this() 确保 jthread 回调持有 shared_ptr，
    // 避免调用方提前释放 Thread 导致 process 访问已析构对象
    thread->worker_ = std::jthread([weak = thread->weak_from_this()](std::stop_token stopToken) {
        if (auto self = weak.lock()) {
            self->process(stopToken);
        }
    });
    return thread;
}

Thread::Thread(std::string name) : name_(std::move(name)), lastRunTimeStamp_(0) {}

Thread::~Thread() noexcept {
    SAssert(worker_.get_id() != std::this_thread::get_id(),
            "error, release itself in the current thread");
    stop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Thread::stop() noexcept {
    std::vector<TimerId> timersToCancel;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_.get_stop_token().stop_requested()) {
            return;
        }
        worker_.request_stop();
        isRunning_ = false;
        timersToCancel = std::move(timerIds_);
        repeatTasks_.clear();
    }
    cond_.notify_all();
    auto& mgr = TimerManager::shareInstance();
    for (auto& id : timersToCancel) {
        mgr.cancel(id);
    }
}

void Thread::pause() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isRunning_) {
        return;
    }
    isRunning_ = false;
}

void Thread::start() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (isRunning_ || worker_.get_stop_token().stop_requested()) {
            return;
        }
        isRunning_ = true;
    }
    cond_.notify_all();
}

Expected<void> Thread::schedule(Task task) noexcept {
    if (worker_.get_stop_token().stop_requested()) {
        return std::unexpected(Error::InvalidOperation);
    }
    taskQueue_.push(std::move(task));
    cond_.notify_one();
    return {};
}

TimerId Thread::schedule(milliseconds delay, Task task) noexcept {
    return registerTimer(
        TimerManager::shareInstance().runAfter(delay, wrapOneShotTask(std::move(task))));
}

TimerId Thread::schedule(Time::TimePoint timePoint, Task task) noexcept {
    return registerTimer(
        TimerManager::shareInstance().runAt(timePoint, wrapOneShotTask(std::move(task))));
}

TimerId Thread::scheduleRepeat(milliseconds interval, Task task) noexcept {
    auto id = TimerId::next();
    std::lock_guard<std::mutex> lock(mutex_);
    repeatTasks_.push_back(RepeatTask{
        .id = id,
        .interval = interval,
        .task = std::make_shared<Task>(std::move(task)),
        .lastRunTime = Time::TimePoint{0},
    });
    return id;
}

void Thread::cancelSchedule(TimerId id) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::ranges::find(repeatTasks_, id, &RepeatTask::id);
        if (it != repeatTasks_.end()) {
            repeatTasks_.erase(it);
            return;
        }
        std::erase(timerIds_, id);
    }
    TimerManager::shareInstance().cancel(id);
}

void Thread::process(std::stop_token stopToken) noexcept {
    while (!stopToken.stop_requested()) {
        if (!isInit_) {
            setup();
        }

        // 先检查 repeat task 是否到期
        bool hasDueRepeatTask = checkRepeatTaskDue();

        // 如果 repeat task 到期，优先处理
        if (hasDueRepeatTask) {
            runRepeatTasks();
        }

        drainTaskQueue();

        bool running;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running = isRunning_;
        }

        if (running) {
            lastRunTimeStamp_ = Time::nowTimeStamp().point();

            auto waitTime = std::chrono::milliseconds::max();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto now = Time::nowTimeStamp();
                for (const auto& rt : repeatTasks_) {
                    if (rt.lastRunTime.point() == 0) {
                        waitTime = 0ms;
                        break;
                    }
                    auto elapsed = (now - rt.lastRunTime).toMilliSeconds();
                    auto remaining = rt.interval - elapsed;
                    if (remaining <= 0ms) {
                        waitTime = 0ms;
                        break;
                    }
                    waitTime = std::min(waitTime, remaining);
                }
            }

            if (waitTime > 0ms && waitTime != std::chrono::milliseconds::max()) {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_.wait_for(lock, stopToken, waitTime, [this] {
                    return !isRunning_ || !taskQueue_.empty();
                });
            } else if (waitTime == std::chrono::milliseconds::max()) {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_.wait(lock, [this, &stopToken] {
                    return stopToken.stop_requested() || !isRunning_ || !taskQueue_.empty();
                });
            }
        } else {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this, &stopToken] {
                return stopToken.stop_requested() || isRunning_ || !taskQueue_.empty();
            });
            if (!stopToken.stop_requested()) {
                lock.unlock();
                drainTaskQueue();
            }
        }
    }
}

bool Thread::checkRepeatTaskDue() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = Time::nowTimeStamp();
    for (const auto& rt : repeatTasks_) {
        if (rt.lastRunTime.point() == 0) {
            return true;
        }
        auto elapsed = (now - rt.lastRunTime).toMilliSeconds();
        if (elapsed >= rt.interval) {
            return true;
        }
    }
    return false;
}

void Thread::runRepeatTasks() noexcept {
    // 按 TimerId 收集到期任务，避免索引在并发删除下错位
    std::vector<TimerId> dueIds;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = Time::nowTimeStamp();
        for (const auto& rt : repeatTasks_) {
            if (rt.lastRunTime.point() == 0) {
                dueIds.push_back(rt.id);
                continue;
            }
            auto elapsed = (now - rt.lastRunTime).toMilliSeconds();
            if (elapsed >= rt.interval) {
                dueIds.push_back(rt.id);
            }
        }
    }

    for (auto dueId : dueIds) {
        std::shared_ptr<Task> taskPtr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::ranges::find(repeatTasks_, dueId, &RepeatTask::id);
            if (it != repeatTasks_.end() && it->task && *it->task) {
                taskPtr = it->task; // shared_ptr 引用计数增加，避免悬空
            }
        }

        if (taskPtr && *taskPtr) {
            (*taskPtr)();
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::ranges::find(repeatTasks_, dueId, &RepeatTask::id);
            if (it != repeatTasks_.end()) {
                it->lastRunTime = Time::nowTimeStamp();
            }
        }
    }
}

void Thread::drainTaskQueue() noexcept {
    auto tasks = taskQueue_.tryPopAll();
    for (auto& task : tasks) {
        if (task) {
            task();
        }
    }
}

void Thread::setThreadName(std::string_view nameView) noexcept {
    std::lock_guard lock(mutex_);
    name_ = std::string(nameView);
    isInit_ = false;
}

void Thread::setup() noexcept {
    std::string name;
    {
        std::lock_guard lock(mutex_);
        name = name_;
    }
#ifndef __APPLE__
    auto handle = worker_.native_handle();
    auto truncated = name.substr(0, 15);
    pthread_setname_np(handle, truncated.c_str());
#else
    pthread_setname_np(name.c_str());
#endif
    isInit_ = true;
}

TimerTask Thread::wrapOneShotTask(Task task) noexcept {
    return [weak = weak_from_this(), task = std::move(task)]() mutable {
        if (auto self = weak.lock()) {
            self->schedule(std::move(task));
        }
    };
}

TimerId Thread::registerTimer(TimerId id) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    timerIds_.push_back(id);
    return id;
}

} // end namespace slark
