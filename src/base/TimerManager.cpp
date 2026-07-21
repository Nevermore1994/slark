//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "TimerManager.h"

#include "Log.hpp"
#include "ThreadPool.hpp"

#include <exception>

namespace slark {
using namespace std::chrono_literals;

namespace {
ThreadPool& timerCallbackPool() {
    static ThreadPool pool(ThreadPoolConfig{.threadCount = 4});
    return pool;
}

template <typename F>
void postTaskToPool(F&& task, TimerId timerId) {
    timerCallbackPool().postJob([task = std::move(task), timerId]() mutable {
        try {
            task();
        } catch (const std::exception& e) {
            LogE("Timer task exception, id={}: {}", timerId.id(), e.what());
        } catch (...) {
            LogE("Timer task unknown exception, id={}", timerId.id());
        }
    });
}
} // namespace

TimerManager& TimerManager::shareInstance() {
    static TimerManager instance;
    return instance;
}

TimerManager::TimerManager()
    : tickThread_([this] {
          tickLoop();
      }) {}

TimerManager::~TimerManager() {
    stopRequested_.store(true);
    cond_.notify_all();
    if (tickThread_.joinable()) {
        tickThread_.join();
    }
    std::lock_guard lock(mutex_);
    timerPool_.clear();
    loopTasks_.clear();
}

void TimerManager::tickLoop() {
    while (!stopRequested_.load()) {
        try {
            {
                std::unique_lock lock(mutex_);
                auto waitTime = timerPool_.peekWaitTime();
                cond_.wait_for(lock, waitTime, [this] {
                    return hasNewTimer_ || stopRequested_.load();
                });
                hasNewTimer_ = false;
            }

            if (stopRequested_.load()) {
                break;
            }

            std::vector<TimerEntry> expired;
            {
                std::lock_guard lock(mutex_);
                timerPool_.collectExpired(expired);
            }

            for (auto& entry : expired) {
                if (!entry.isValid) {
                    continue;
                }
                if (entry.isLoop) {
                    processLoopTask(entry);
                } else if (entry.task) {
                    postTaskToPool(
                        [task = std::move(entry.task)]() mutable {
                            task();
                        },
                        entry.id);
                }
            }
        } catch (const std::exception& e) {
            LogE("Timer tickLoop exception: {}", e.what());
        } catch (...) {
            LogE("Timer tickLoop unknown exception");
        }
    }
}

void TimerManager::processLoopTask(TimerEntry& entry) {
    std::shared_ptr<LoopTaskWrapper> sharedTask;
    {
        std::lock_guard lock(mutex_);
        if (auto it = loopTasks_.find(entry.id.id()); it != loopTasks_.end()) {
            sharedTask = it->second;
        }
    }
    if (!sharedTask) {
        return;
    }

    // 先重新插入定时器
    entry.expireTime = Time::nowTimeStamp() + entry.loopInterval;
    entry.isValid = true;
    {
        std::lock_guard lock(mutex_);
        timerPool_.reinsert(std::move(entry));
    }

    postTaskToPool(
        [sharedTask] {
            (*sharedTask)();
        },
        entry.id);
}

TimerId TimerManager::addTimer(Time::TimePoint expireTime,
                               TimerTask func,
                               bool isLoop,
                               std::chrono::milliseconds interval) noexcept {
    auto id = TimerId::next();
    TimerEntry entry{
        .id = id,
        .expireTime = expireTime,
        .task = std::move(func),
        .isLoop = isLoop,
        .loopInterval = interval,
        .isValid = true,
    };
    {
        std::lock_guard lock(mutex_);
        timerPool_.addTimer(std::move(entry));
        hasNewTimer_ = true;
    }
    cond_.notify_one();
    return id;
}

TimerId TimerManager::runAt(Time::TimePoint timePoint, TimerTask func) noexcept {
    return addTimer(timePoint, std::move(func), false, 0ms);
}

TimerId TimerManager::runAfter(std::chrono::milliseconds delay, TimerTask func) noexcept {
    return addTimer(Time::nowTimeStamp() + delay, std::move(func), false, 0ms);
}

TimerId TimerManager::runLoop(std::chrono::milliseconds interval, TimerTask func) noexcept {
    auto id = TimerId::next();

    // 循环任务需要多次执行，用 shared_ptr 包装
    auto sharedTask = std::make_shared<LoopTaskWrapper>(LoopTaskWrapper{.task = std::move(func)});

    {
        std::lock_guard lock(mutex_);
        loopTasks_[id.id()] = sharedTask;

        // 定时器 entry 中存储一个空 task，实际执行时从 loopTasks_ 获取
        TimerEntry entry{
            .id = id,
            .expireTime = Time::nowTimeStamp() + interval,
            .task = nullptr, // 循环任务不在此存储
            .isLoop = true,
            .loopInterval = interval,
            .isValid = true,
        };
        timerPool_.addTimer(std::move(entry));
        hasNewTimer_ = true;
    }
    cond_.notify_one();
    return id;
}

void TimerManager::cancel(TimerId id) noexcept {
    std::lock_guard lock(mutex_);
    timerPool_.cancel(id);
    loopTasks_.erase(id.id());
}

void TimerManager::clear() noexcept {
    std::lock_guard lock(mutex_);
    timerPool_.clear();
    loopTasks_.clear();
}

} // end namespace slark
