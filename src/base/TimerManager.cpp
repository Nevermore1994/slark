//
// Created by Nevermore on 2021/10/22.
// slark TimerManager
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include <chrono>
#include <ranges>
#include "TimerManager.h"

namespace slark {
using namespace std::chrono_literals;

TimerManager& TimerManager::shareInstance() {
    static TimerManager instance;
    return instance;
}

TimerManager::TimerManager()
    : timerThread_(std::make_unique<Thread>("TimerManager", &TimerManager::loop, this)) {
    timerThread_->start();
}

TimerManager::~TimerManager() {
    {
        std::lock_guard lock(mutex_);
        isExited_ = true;
    }
    pool_.clear();
    timerThread_->stop();
}

void TimerManager::loop() {
    {
        std::unique_lock lock(mutex_);
        cond_.wait(lock, [this](){
            return !pool_.empty() || isExited_;
        });
        if (!isExited_) {
            return;
        }
    }
    std::vector<std::thread> workers;
    pool_.loop([&](ExecuteMode mode, TimerTask&& task) {
        if (mode == ExecuteMode::Serial) {
            task();
        } else {
            workers.emplace_back([task = std::move(task)](){
                task();
            });
        }
    });
    std::ranges::for_each(workers, std::mem_fn(&std::thread::join));
}

TimerId TimerManager::runAt(Time::TimePoint timeStamp, TimerTask func, ExecuteMode mode) noexcept {
    auto timerId = pool_.runAt(timeStamp, std::move(func), mode);
    cond_.notify_all();
    return timerId;
}

TimerId TimerManager::runAfter(std::chrono::milliseconds delayTime, TimerTask func, ExecuteMode mode) noexcept {
    auto timerId = pool_.runAfter(delayTime, std::move(func), mode);
    cond_.notify_all();
    return timerId;
}

TimerId TimerManager::runLoop(std::chrono::milliseconds timeInterval, TimerTask func, ExecuteMode mode) noexcept {
    auto timerId = pool_.runLoop(timeInterval, std::move(func), mode);
    cond_.notify_all();
    return timerId;
}

void TimerManager::cancel(TimerId id) noexcept {
    pool_.cancel(id);
}

void TimerManager::clear() noexcept {
    pool_.clear();
}

}//end namespace slark
