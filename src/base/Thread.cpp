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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isRunning_ = false;
    }
    cond_.notify_all();
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
    if (worker_.get_stop_token().stop_requested()) {
        return TimerId{};
    }
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

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this, &stopToken] {
                return stopToken.stop_requested() || isRunning_;
            });
            if (!isRunning_) {
                continue;
            }
        }

        if (stopToken.stop_requested()) {
            break;
        }

        lastRunTimeStamp_ = Time::nowTimeStamp().point();

        auto waitTime = runRepeatTasks();
        drainTaskQueue();

        if (waitTime == 0ms) {
            continue;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        if (waitTime == std::chrono::milliseconds::max()) {
            cond_.wait(lock, [this, &stopToken] {
                return stopToken.stop_requested() || !isRunning_;
            });
        } else {
            cond_.wait_for(lock, stopToken, waitTime, [this] {
                return !isRunning_;
            });
        }
    }
}

milliseconds Thread::runRepeatTasks() noexcept {
    // 一次加锁，收集到期任务和 taskPtr，同时计算下次最小等待时间
    struct DueTask {
        TimerId id;
        std::shared_ptr<Task> taskPtr;
    };

    std::vector<DueTask> dueTasks;
    auto waitTime = std::chrono::milliseconds::max();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = Time::nowTimeStamp();
        for (const auto& rt : repeatTasks_) {
            bool due = rt.lastRunTime.point() == 0;
            if (!due) {
                auto elapsed = (now - rt.lastRunTime).toMilliSeconds();
                auto remaining = rt.interval - elapsed;
                if (remaining <= 0ms) {
                    due = true;
                } else {
                    waitTime = std::min(waitTime, remaining);
                }
            }
            if (due && rt.task && *rt.task) {
                dueTasks.push_back({rt.id, rt.task});
            }
        }
    }

    if (dueTasks.empty()) {
        return waitTime;
    }

    for (auto& [id, taskPtr] : dueTasks) {
        (*taskPtr)();
    }

    // 批量更新 lastRunTime
    auto now = Time::nowTimeStamp();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, _] : dueTasks) {
            auto it = std::ranges::find(repeatTasks_, id, &RepeatTask::id);
            if (it != repeatTasks_.end()) {
                it->lastRunTime = now;
                // 执行完后计算剩余时间
                waitTime = std::min(waitTime, it->interval);
            }
        }
    }

    return waitTime;
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
