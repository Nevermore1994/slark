//
// Created by Nevermore on 2021/10/25.
// slark TimerPool
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "TimerPool.hpp"
#include "Utility.hpp"
#include "Log.hpp"
#include <chrono>

using namespace slark;
using namespace std::chrono;
using namespace std::chrono_literals;

TimerPool::~TimerPool() {
    clear();
}

TimerPool::TimerPool(TimerPool&& lhs) noexcept {
    clear();
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
    std::unique_lock<std::mutex> lock(mutex_);
    timerInfos_.swap(temp);
    timers_.clear();
}

void TimerPool::loop() noexcept {
    uint64_t now = Time::nowTimeStamp();
    std::vector<TimerInfo> expireTimes;
    decltype(timers_) timers;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while(!timerInfos_.empty() && now >= timerInfos_.top().expireTime) {
            auto expireTimeInfo = timerInfos_.top();
            expireTimes.push_back(expireTimeInfo);
            timerInfos_.pop();
        }
        timers.insert(timers_.begin(), timers_.end());
    }
    for(auto& expireTimeInfo: expireTimes) {
        auto id = expireTimeInfo.timerId;
        bool canRemove = timers.count(id) == 0;
        
        if(canRemove) {
            continue;
        }
        auto& timer = timers[id];
        if(timer.isValid) {
            timer.func();
        }
        canRemove = !timer.isValid || !timer.timerInfo.isLoop;
        if(canRemove) {
            remove(id);
        } else {
            TimerInfo timerInfo = timer.timerInfo;
            timerInfo.expireTime = now + timerInfo.loopInterval;
            //add loop timer
            std::unique_lock<std::mutex> lock(mutex_);
            timerInfos_.push(timerInfo);
        }
    }
}

TimerId TimerPool::addTimer(uint64_t timeStamp, TimerCallback func, bool isLoop, uint64_t timeInterval) noexcept {
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

TimerId TimerPool::runAt(uint64_t timeStamp, TimerCallback func) noexcept {
    return addTimer(timeStamp, std::move(func), false);
}

TimerId TimerPool::runAfter(uint64_t delayTime, TimerCallback func) noexcept {
    auto now = Time::nowTimeStamp();
    return addTimer(now + delayTime * 1000, std::move(func), false);
}

TimerId TimerPool::runLoop(uint64_t timeInterval, TimerCallback func) noexcept {
    auto now = Time::nowTimeStamp();
    return addTimer(now + timeInterval * 1000, std::move(func), true, timeInterval * 1000);
}

TimerId TimerPool::runAfter(milliseconds delayTime, TimerCallback func) noexcept {
    auto now = Time::nowTimeStamp();
    auto t = std::chrono::duration_cast<microseconds>(delayTime).count();
    return addTimer(now + t, std::move(func), false);
}

TimerId TimerPool::runLoop(milliseconds timeInterval, TimerCallback func) noexcept {
    auto now = Time::nowTimeStamp();
    auto t = std::chrono::duration_cast<microseconds>(timeInterval).count();
    
    return addTimer(now + t, std::move(func), true, t);
}

void TimerPool::cancel(TimerId id) {
    std::unique_lock<std::mutex> lock(mutex_);
    if(timers_.count(id)) {
        timers_[id].isValid = false;
    }
}

void TimerPool::remove(TimerId id) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    timers_.erase(id);
}

