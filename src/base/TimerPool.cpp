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

void TimerPool::clear() noexcept {
    decltype(timerInfos_) temp;
    std::lock_guard<std::mutex> lock(mutex_);
    timerInfos_.swap(temp);
    timers_.clear();
}

bool TimerPool::empty() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return timerInfos_.empty();
}

std::chrono::milliseconds TimerPool::peekActiveTime() const noexcept {
    auto now = Time::nowTimeStamp();
    std::unique_lock<std::mutex> lock(mutex_);
    if (timerInfos_.empty()) {
        return std::chrono::milliseconds(1000); //1s
    }
    auto activeTime = timerInfos_.top().expireTime;
    return (activeTime - now).toMilliSeconds();
}

void TimerPool::loop(std::function<void(ExecuteMode, TimerTask&&)>&& doTask) noexcept {
    std::vector<TimerInfo> expiredTimers;
    {
        auto now = Time::nowTimeStamp();
        std::unique_lock<std::mutex> lock(mutex_);
        while (!timerInfos_.empty() && now >= timerInfos_.top().expireTime) {
            expiredTimers.push_back(timerInfos_.top());
            timerInfos_.pop();
        }
    }

    if (expiredTimers.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& expiredTimer : expiredTimers) {
        auto it = timers_.find(expiredTimer.timerId);
        if (it == timers_.end()) {
            continue;
        }

        Timer& timer = it->second;
        if (timer.isValid && timer.func) {
            auto task = timer.func;
            mutex_.unlock();
            doTask(expiredTimer.mode, std::move(task));
            mutex_.lock();

            it = timers_.find(expiredTimer.timerId);
            if (it == timers_.end() || !it->second.isValid) {
                continue;
            }
        }

        if (!timer.isValid || !timer.timerInfo.isLoop) {
            timers_.erase(it);
        } else {
            TimerInfo newInfo = timer.timerInfo;
            newInfo.expireTime = Time::nowTimeStamp() + newInfo.loopInterval;
            timerInfos_.push(newInfo);
        }
    }
}

void TimerPool::loop() noexcept {
    loop([](ExecuteMode, TimerTask&& task){
        task();
    });
}

TimerId TimerPool::addTimer(Time::TimePoint timeStamp,
                            TimerTask func,
                            bool isLoop,
                            std::chrono::milliseconds timeInterval,
                            ExecuteMode mode) noexcept {
    Timer timer(timeStamp, std::move(func));
    timer.timerInfo.isLoop = isLoop;
    timer.timerInfo.loopInterval = timeInterval;
    timer.timerInfo.mode = mode;
    auto id = Random::u32Random();
    timer.timerInfo.timerId = id;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        timerInfos_.push(timer.timerInfo);
        timers_.insert(std::make_pair(id, std::move(timer)));
    }
    return id;
}

TimerId TimerPool::runAt(Time::TimePoint timeStamp, TimerTask func, ExecuteMode mode) noexcept {
    return addTimer(timeStamp, std::move(func), false, 0ms, mode);
}

TimerId TimerPool::runAfter(milliseconds delayTime, TimerTask func, ExecuteMode mode) noexcept {
    auto now = Time::nowTimeStamp();
    return addTimer(now + delayTime, std::move(func), false, 0ms, mode);
}

TimerId TimerPool::runLoop(milliseconds timeInterval, TimerTask func, ExecuteMode mode) noexcept {
    return addTimer(Time::nowTimeStamp() + timeInterval, std::move(func), true, timeInterval, mode);
}

void TimerPool::cancel(TimerId id) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = timers_.find(id); it != timers_.end()) {
        it->second.isValid = false;
    }
}

void TimerPool::cancel(const std::vector<TimerId>& timers) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ranges::for_each(timers, [this](const TimerId& id){
        if (auto it = timers_.find(id); it != timers_.end()) {
            it->second.isValid = false;
        }
    });
}

}//end namespace slark
