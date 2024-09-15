//
// Created by Nevermore on 2021/10/25.
// slark TimerPool
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "TimerPool.h"
#include "Random.hpp"
#include "Log.hpp"
#include <chrono>
#include <ranges>
#include <future>

namespace slark {

using std::chrono::milliseconds;
using namespace std::chrono_literals;

TimerPool::~TimerPool() {
    clear();
}

TimerPool::TimerPool(TimerPool&& lhs) noexcept {
    timerInfos_.swap(lhs.timerInfos_);
    timers_.swap(lhs.timers_);
}

TimerPool& TimerPool::operator=(TimerPool&& lhs) noexcept {
    timerInfos_.swap(lhs.timerInfos_);
    timers_.swap(lhs.timers_);
    return *this;
}

void TimerPool::clear() noexcept {
    decltype(timerInfos_) temp;
    std::lock_guard<std::mutex> lock(mutex_);
    timerInfos_.swap(temp);
    timers_.clear();
}

void TimerPool::loop() noexcept {
    auto now = Time::nowTimeStamp();
    std::vector<TimerInfo> expireTimes;
    decltype(timers_) timers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!timerInfos_.empty() && now >= timerInfos_.top().expireTime) {
            auto expireTimeInfo = timerInfos_.top();
            expireTimes.push_back(expireTimeInfo);
            timerInfos_.pop();
        }
        timers.insert(timers_.begin(), timers_.end());
        if (expireTimes.empty()) {
            return;
        }
    }
    for (auto& expireTimeInfo : expireTimes) {
        auto id = expireTimeInfo.timerId;

        if (timers.count(id) == 0) {
            continue;
        }
        auto& timer = timers[id];
        if (timer.isValid && timer.func) {
            timer.func();
        }
        if (!timer.isValid || !timer.timerInfo.isLoop) {
            remove(id);
        } else {
            //add timer again
            TimerInfo timerInfo = timer.timerInfo;
            timerInfo.expireTime = now + timerInfo.loopInterval;
            std::unique_lock<std::mutex> lock(mutex_);
            timerInfos_.push(timerInfo);
        }
    }
}

TimerId TimerPool::addTimer(Time::TimePoint timeStamp, TimerTask func, bool isLoop, std::chrono::milliseconds timeInterval) noexcept {
    Timer timer(timeStamp, std::move(func));
    timer.timerInfo.isLoop = isLoop;
    timer.timerInfo.loopInterval = timeInterval;
    TimerId id = timer.timerInfo.timerId;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        timerInfos_.push(timer.timerInfo);
        timers_.insert(std::make_pair(id, std::move(timer)));
    }
    return id;
}

TimerId TimerPool::runAt(Time::TimePoint timeStamp, TimerTask func) noexcept {
    return addTimer(timeStamp, std::move(func), false);
}

TimerId TimerPool::runAfter(milliseconds delayTime, TimerTask func) noexcept {
    auto now = Time::nowTimeStamp();
    return addTimer(now + delayTime, std::move(func), false);
}

TimerId TimerPool::runLoop(milliseconds timeInterval, TimerTask func) noexcept {
    return addTimer(Time::nowTimeStamp() + timeInterval, std::move(func), true, timeInterval);
}

void TimerPool::cancel(TimerId id) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (auto it = timers_.find(id); it != timers_.end()) {
        it->second.isValid = false;
    }
}

void TimerPool::cancel(const std::vector<TimerId>& timers) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    std::ranges::for_each(timers, [this](const TimerId& id){
        if (auto it = timers_.find(id); it != timers_.end()) {
            it->second.isValid = false;
        }
    });
}

void TimerPool::remove(TimerId id) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    timers_.erase(id);
}

}//end namespace slark
