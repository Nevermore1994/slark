//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <functional>
#include <thread>

#ifndef __APPLE__
#include <pthread.h>
#endif

#include "Log.hpp"
#include "Thread.h"
#include "TimerManager.h"

namespace slark {
using std::chrono::milliseconds;
using namespace std::chrono_literals;

namespace {
thread_local Thread* tlsCurrentThread = nullptr;
}  // namespace

Thread* Thread::current() noexcept {
    return tlsCurrentThread;
}

void Thread::setCurrentActive(bool active) noexcept {
    if (tlsCurrentThread) {
        tlsCurrentThread->setActive(active);
    }
}

namespace _channel_detail {
void setCurrentThreadActive(bool active) noexcept {
    Thread::setCurrentActive(active);
}
}  // namespace _channel_detail

Thread::ScopedInactive::ScopedInactive() noexcept : thread_(Thread::current()) {
    if (thread_) {
        thread_->setActive(false);
    }
}

Thread::ScopedInactive::~ScopedInactive() noexcept {
    if (thread_) {
        thread_->setActive(true);
    }
}

std::shared_ptr<Thread> Thread::create(std::string name) {
    auto thread = std::shared_ptr<Thread>(new Thread(std::move(name)));
    thread->worker_ = std::thread([weak = thread->weak_from_this()]() {
        if (auto self = weak.lock()) {
            self->process();
        }
    });
    return thread;
}

std::shared_ptr<Thread> Thread::createWithLoop(std::string name,
                                               std::function<void(Thread&)> loop) {
    auto thread = std::shared_ptr<Thread>(new Thread(std::move(name)));
    thread->customWork_ = std::move(loop);
    {
        std::lock_guard<std::mutex> lock(thread->mutex_);
        thread->isRunning_ = true;
    }
    thread->worker_ = std::thread([weak = thread->weak_from_this()]() {
        if (auto self = weak.lock()) {
            self->process();
        }
    });
    return thread;
}

Thread::Thread(std::string name) : name_(std::move(name)), lastRunTimeStamp_(0) {}

Thread::~Thread() noexcept {
    stop();
    bool isCurrentThread = worker_.get_id() == std::this_thread::get_id();
    if (worker_.joinable()) {
        if (isCurrentThread) {
            worker_.detach();
        } else {
            worker_.join();
        }
    }
}

void Thread::stop() noexcept {
    std::vector<TimerId> timersToCancel;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopRequested_.load(std::memory_order_acquire)) {
            return;
        }
        stopRequested_.store(true, std::memory_order_release);
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

void Thread::join() noexcept {
    // Must not be called on the worker's own thread, otherwise self-join deadlocks
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
    }
}

void Thread::pause() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!isRunning_) {
            return;
        }
        isRunning_ = false;
    }
    cond_.notify_all();
}

void Thread::start() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (isRunning_ || stopRequested_.load(std::memory_order_acquire)) {
            return;
        }
        isRunning_ = true;
    }
    cond_.notify_all();
}

Expected<void> Thread::postTask(ThreadTask task) noexcept {
    if (stopRequested_.load(std::memory_order_acquire)) {
        return std::unexpected(Error::InvalidOperation);
    }
    {
        // Mutual exclusion with the predicate check in process()'s cond_.wait (under mutex_)
        // avoids a lost wakeup.
        std::lock_guard<std::mutex> lock(mutex_);
        taskQueue_.push(std::move(task));
    }
    cond_.notify_one();
    return {};
}

TimerId Thread::postTask(milliseconds delay, ThreadTask task) noexcept {
    return registerTimer(
        TimerManager::shareInstance().runAfter(delay, wrapOneShotTask(std::move(task))));
}

TimerId Thread::postTask(Time::TimePoint timePoint, ThreadTask task) noexcept {
    return registerTimer(
        TimerManager::shareInstance().runAt(timePoint, wrapOneShotTask(std::move(task))));
}

TimerId Thread::postRepeatTask(milliseconds interval, ThreadTask task) noexcept {
    if (stopRequested_.load(std::memory_order_acquire)) {
        return TimerId{};
    }
    auto id = TimerId::next();
    std::lock_guard<std::mutex> lock(mutex_);
    repeatTasks_.push_back(RepeatTask{
        .id = id,
        .interval = interval,
        .task = std::make_shared<ThreadTask>(std::move(task)),
        .lastRunTime = Time::TimePoint{0},
    });
    return id;
}

void Thread::cancelRepeatTask(TimerId id) noexcept {
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

void Thread::waitIdle(std::chrono::milliseconds waitTime,
                     std::unique_lock<std::mutex>& lock) noexcept {
    ScopedInactive guard;
    if (waitTime == std::chrono::milliseconds::max()) {
        cond_.wait(lock, [this] {
            return stopRequested_.load(std::memory_order_acquire) || !isRunning_ ||
                   !taskQueue_.empty();
        });
    } else {
        cond_.wait_for(lock, waitTime, [this] {
            return stopRequested_.load(std::memory_order_acquire) || !isRunning_ ||
                   !taskQueue_.empty();
        });
    }
}

void Thread::process() noexcept {
    tlsCurrentThread = this;
    setup();
    setActive(true);

    if (customWork_) {
        customWork_(*this);
        stopRequested_.store(true, std::memory_order_release);
    } else {
        while (!stopRequested_.load(std::memory_order_acquire)) {
            if (!isInit_) {
                setup();
            }

            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!isRunning_ && !stopRequested_.load(std::memory_order_acquire)) {
                    ScopedInactive guard;
                    cond_.wait(lock, [this] {
                        return stopRequested_.load(std::memory_order_acquire) || isRunning_;
                    });
                }
                if (!isRunning_) {
                    continue;
                }
            }

            if (stopRequested_.load(std::memory_order_acquire)) {
                break;
            }

            setActive(true);

            auto waitTime = runRepeatTasks();
            drainTaskQueue();

            if (waitTime == 0ms) {
                continue;
            }

            std::unique_lock<std::mutex> lock(mutex_);
            waitIdle(waitTime, lock);
        }
    }

    setActive(false);
    tlsCurrentThread = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    isRunning_ = false;
}

milliseconds Thread::runRepeatTasks() noexcept {
    // Collect due tasks and taskPtr in one lock acquisition, computing the next minimum wait time
    struct DueThreadTask {
        TimerId id;
        std::shared_ptr<ThreadTask> taskPtr;
    };

    std::vector<DueThreadTask> dueTasks;
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

    auto now = Time::nowTimeStamp();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, _] : dueTasks) {
            auto it = std::ranges::find(repeatTasks_, id, &RepeatTask::id);
            if (it != repeatTasks_.end()) {
                it->lastRunTime = now;
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

TimerTask Thread::wrapOneShotTask(ThreadTask task) noexcept {
    return [weak = weak_from_this(), task = std::move(task)]() mutable {
        if (auto self = weak.lock()) {
            self->postTask(std::move(task));
        }
    };
}

TimerId Thread::registerTimer(TimerId id) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    timerIds_.push_back(id);
    return id;
}

bool Thread::shouldStop() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!isRunning_ && !stopRequested_.load(std::memory_order_acquire)) {
        ScopedInactive guard;
        cond_.wait(lock, [this] {
            return stopRequested_.load(std::memory_order_acquire) || isRunning_;
        });
    }
    return stopRequested_.load(std::memory_order_acquire);
}

void Thread::drainPendingWork() noexcept {
    runRepeatTasks();
    drainTaskQueue();
}

}  // end namespace slark
