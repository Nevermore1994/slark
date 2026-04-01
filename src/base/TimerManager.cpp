//
// Created by Nevermore on 2021/10/22.
// slark TimerManager
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "TimerManager.h"

#include "Log.hpp"

namespace slark {
using namespace std::chrono_literals;

TimerManager& TimerManager::shareInstance() {
    static TimerManager instance;
    return instance;
}

TimerManager::TimerManager()
    : tickThread_([this](std::stop_token token) {
          tickLoop(token);
      }) {}

TimerManager::~TimerManager() {
    tickThread_.request_stop();
    cond_.notify_all();
    std::lock_guard lock(mutex_);
    timerPool_.clear();
}

void TimerManager::tickLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        // 等待到期或被新定时器唤醒
        {
            std::unique_lock lock(mutex_);
            auto waitTime = timerPool_.peekWaitTime();
            cond_.wait_for(lock, stopToken, waitTime, [this] {
                return hasNewTimer_;
            });
            hasNewTimer_ = false;
        }

        if (stopToken.stop_requested()) {
            break;
        }

        std::vector<TimerEntry> expired;
        {
            std::lock_guard lock(mutex_);
            timerPool_.collectExpired(expired);
        }

        // 锁外执行回调
        for (auto& entry : expired) {
            if (!entry.isValid || !entry.task) {
                continue;
            }
            try {
                entry.task();
            } catch (const std::exception& e) {
                LogE("Timer task exception: {}", e.what());
            } catch (...) {
                LogE("Timer task unknown exception");
            }

            if (entry.isLoop) {
                entry.expireTime = Time::nowTimeStamp() + entry.loopInterval;
                entry.isValid = true;
                std::lock_guard lock(mutex_);
                timerPool_.reinsert(std::move(entry));
            }
        }
    }
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
    return addTimer(Time::nowTimeStamp() + interval, std::move(func), true, interval);
}

void TimerManager::cancel(TimerId id) noexcept {
    std::lock_guard lock(mutex_);
    timerPool_.cancel(id);
}

void TimerManager::clear() noexcept {
    std::lock_guard lock(mutex_);
    timerPool_.clear();
}

} // end namespace slark
