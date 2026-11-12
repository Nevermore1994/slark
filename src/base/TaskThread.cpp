//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "TaskThread.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "TimerManager.h"

namespace slark {
using namespace std::chrono_literals;

TaskThread::TaskThread(std::string name, LaunchMode mode)
    : Thread(std::move(name)), mode_(mode), launched_(mode == LaunchMode::Immediate) {}

// Base dtor's stop() dispatches to Thread::stop; clear timer registration here.
TaskThread::~TaskThread() noexcept {
    cancelOutstanding();
}

std::shared_ptr<TaskThread> TaskThread::create(std::string name, LaunchMode mode) {
    auto thread = std::shared_ptr<TaskThread>(new TaskThread(std::move(name), mode));
    try {
        thread->startWorker(ThreadTask([ptr = thread.get()] {
            ptr->runLoop();
        }));
    } catch (...) {
        return nullptr;
    }
    return thread;
}

bool TaskThread::isRunning() const noexcept {
    return launched_.load(std::memory_order_acquire) && Thread::isRunning();
}

void TaskThread::stop() noexcept {
    Thread::stop();
    cancelOutstanding();
}

void TaskThread::launch() noexcept {
    if (mode_ == LaunchMode::Immediate) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        if (isStopRequested()) {
            return;
        }
        launched_.store(true, std::memory_order_release);
    }
    cond_.notify_all();
}

TimerId TaskThread::postTask(std::chrono::milliseconds delay, ThreadTask task) noexcept {
    const auto now = Time::nowTimeStamp();
    const auto nonnegative = std::max(delay, 0ms);
    return postOneShot(now + Time::TimeDelta::fromMilliSeconds(nonnegative), std::move(task));
}

TimerId TaskThread::postTask(Time::TimePoint at, ThreadTask task) noexcept {
    return postOneShot(at, std::move(task));
}

TimerId TaskThread::postRepeatTask(std::chrono::milliseconds interval, ThreadTask task) noexcept {
    auto repeat = std::make_shared<Repeat>();
    repeat->id = TimerId::next();
    repeat->interval = std::max(interval, 0ms);
    repeat->task = std::move(task);
    {
        std::lock_guard lock(mutex_);
        if (isStopRequested()) {
            return TimerId{};
        }
        repeatTasks_.push_back(repeat);
        repeatVersion_.fetch_add(1, std::memory_order_release);
    }
    cond_.notify_one();
    return repeat->id;
}

bool TaskThread::cancel(TimerId id) noexcept {
    if (!id.isValid()) {
        return false;
    }

    std::shared_ptr<Repeat> repeat;
    std::shared_ptr<OneShot> oneShot;
    {
        std::lock_guard lock(mutex_);
        auto repeatIt = std::ranges::find(repeatTasks_, id, &Repeat::id);
        if (repeatIt != repeatTasks_.end()) {
            repeat = *repeatIt;
            repeatTasks_.erase(repeatIt);
            repeatVersion_.fetch_add(1, std::memory_order_release);
        } else if (auto oneShotIt = oneShots_.find(id.id()); oneShotIt != oneShots_.end()) {
            oneShot = oneShotIt->second;
            oneShots_.erase(oneShotIt);
        } else {
            return false;
        }
    }

    if (repeat) {
        return repeat->state.exchange(RepeatState::Cancelled, std::memory_order_acq_rel) !=
               RepeatState::Cancelled;
    }

    auto expected = OneShotState::Pending;
    const bool cancelled = oneShot->state.compare_exchange_strong(expected, OneShotState::Cancelled,
                                                                  std::memory_order_acq_rel);
    TimerManager::shareInstance().cancel(id);
    return cancelled;
}

size_t TaskThread::pendingTimerCount() const noexcept {
    std::lock_guard lock(mutex_);
    return oneShots_.size() + repeatTasks_.size();
}

void TaskThread::runLoop() noexcept {
    if (mode_ == LaunchMode::Deferred) {
        std::unique_lock lock(mutex_);
        cond_.wait(lock, [this] {
            return isStopRequested() || launched_.load(std::memory_order_acquire);
        });
    }

    while (!isStopRequested()) {
        const auto repeatVersion = repeatVersion_.load(std::memory_order_acquire);
        auto waitTime = std::chrono::milliseconds::max();
        runRepeatTasks(waitTime);
        drainPendingWork();

        std::unique_lock lock(mutex_);
        if (isStopRequested()) {
            break;
        }
        if (hasPendingWork() || repeatVersion != repeatVersion_.load(std::memory_order_acquire)) {
            continue;
        }
        Thread::ScopedInactive inactive;
        if (waitTime == std::chrono::milliseconds::max()) {
            cond_.wait(lock);
        } else if (waitTime > 0ms) {
            cond_.wait_for(lock, waitTime);
        }
    }
}

void TaskThread::runRepeatTasks(std::chrono::milliseconds& waitTime) noexcept {
    std::vector<std::shared_ptr<Repeat>> due;
    {
        std::lock_guard lock(mutex_);
        const auto now = Time::nowTimeStamp();
        for (const auto& repeat : repeatTasks_) {
            if (repeat->state.load(std::memory_order_acquire) != RepeatState::Idle) {
                continue;
            }
            if (repeat->lastRunTime.point() == 0) {
                due.push_back(repeat);
                continue;
            }
            const auto elapsed = (now - repeat->lastRunTime).toMilliSeconds();
            const auto remaining = repeat->interval - elapsed;
            if (remaining <= 0ms) {
                due.push_back(repeat);
            } else {
                waitTime = std::min(waitTime, remaining);
            }
        }
    }

    for (const auto& repeat : due) {
        if (isStopRequested()) {
            return;
        }
        auto expected = RepeatState::Idle;
        if (!repeat->state.compare_exchange_strong(expected, RepeatState::Running,
                                                   std::memory_order_acq_rel)) {
            continue;
        }
        if (repeat->task) {
            repeat->task();
        }
        const auto finishedAt = Time::nowTimeStamp();
        expected = RepeatState::Running;
        if (repeat->state.compare_exchange_strong(expected, RepeatState::Idle,
                                                  std::memory_order_acq_rel)) {
            std::lock_guard lock(mutex_);
            auto it = std::ranges::find(repeatTasks_, repeat->id, &Repeat::id);
            if (it != repeatTasks_.end()) {
                repeat->lastRunTime = finishedAt;
                waitTime = std::min(waitTime, repeat->interval);
            }
        } else {
            std::lock_guard lock(mutex_);
            std::erase_if(repeatTasks_, [&repeat](const auto& item) {
                return item->id == repeat->id;
            });
        }
    }
}

TimerId TaskThread::postOneShot(Time::TimePoint at, ThreadTask task) noexcept {
    auto oneShot = std::make_shared<OneShot>();
    oneShot->task = std::move(task);
    TimerId id;
    {
        std::lock_guard lock(mutex_);
        if (isStopRequested()) {
            return TimerId{};
        }
        const auto weak = weak_from_this();
        auto timerTask = [weak, oneShot]() mutable {
            auto base = weak.lock();
            if (!base) {
                return;
            }
            auto self = std::static_pointer_cast<TaskThread>(base);
            auto queued = ThreadTask([weak, oneShot] {
                auto owner = weak.lock();
                if (!owner || owner->isStopRequested()) {
                    return;
                }
                auto expected = OneShotState::Pending;
                if (!oneShot->state.compare_exchange_strong(expected, OneShotState::Running,
                                                            std::memory_order_acq_rel)) {
                    return;
                }
                std::static_pointer_cast<TaskThread>(owner)->eraseOneShot(oneShot->id);
                if (oneShot->task) {
                    oneShot->task();
                }
            });
            (void)self->Thread::postTask(std::move(queued));
        };
        id = TimerManager::shareInstance().runAt(at, std::move(timerTask));
        oneShot->id = id;
        oneShots_.emplace(id.id(), oneShot);
    }
    return id;
}

void TaskThread::cancelOutstanding() noexcept {
    std::vector<TimerId> timerIds;
    {
        std::lock_guard lock(mutex_);
        timerIds.reserve(oneShots_.size());
        for (auto& [_, oneShot] : oneShots_) {
            oneShot->state.store(OneShotState::Cancelled, std::memory_order_release);
            timerIds.push_back(oneShot->id);
        }
        oneShots_.clear();
        for (auto& repeat : repeatTasks_) {
            repeat->state.store(RepeatState::Cancelled, std::memory_order_release);
        }
        repeatTasks_.clear();
    }
    if (!timerIds.empty()) {
        auto& timers = TimerManager::shareInstance();
        for (const auto id : timerIds) {
            timers.cancel(id);
        }
    }
}

void TaskThread::eraseOneShot(TimerId id) noexcept {
    std::lock_guard lock(mutex_);
    oneShots_.erase(id.id());
}

}  // namespace slark
