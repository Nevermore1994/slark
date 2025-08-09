//
// Created by Nevermore on 2025/10/22.
// slark TimerManager
// Copyright (c) 2025 Nevermore All rights reserved.
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
    cond_.notify_all(); 
    timerThread_->stop();
    timerPool_.clear();
}

void TimerManager::loop() {
    {
        auto waitTime = timerPool_.peekActiveTime();
        std::unique_lock lock(mutex_);
        cond_.wait_for(lock, waitTime, [this](){
            return isAdded_ || isExited_;
        });
        if (isExited_ || timerPool_.empty()) {
            return;
        }
        isAdded_ = false;
    }

    timerPool_.loop([&](ExecuteMode mode, TimerTask&& task) {
        auto safeTask = [task = std::move(task)]() {
            try {
                task();
            } catch (const std::exception& e) {
                LogE("Timer task exception: {}", e.what());
            } catch (...) {
                LogE("Timer task unknown exception");
            }
        };
        if (mode == ExecuteMode::Serial) {
            safeTask();
        } else {
            auto result = workPool_.submit(safeTask);
            if (result.error()) {
                LogE("Timer task execution error");
            }
        }
    });
}

TimerId TimerManager::runAt(Time::TimePoint timeStamp, TimerTask func, ExecuteMode mode) noexcept {
    auto timerId = timerPool_.runAt(timeStamp, std::move(func), mode);
    isAdded_ = true;
    cond_.notify_one();
    return timerId;
}

TimerId TimerManager::runAfter(std::chrono::milliseconds delayTime, TimerTask func, ExecuteMode mode) noexcept {
    auto timerId = timerPool_.runAfter(delayTime, std::move(func), mode);
    isAdded_ = true;
    cond_.notify_one();
    return timerId;
}

TimerId TimerManager::runLoop(std::chrono::milliseconds timeInterval, TimerTask func, ExecuteMode mode) noexcept {
    auto timerId = timerPool_.runLoop(timeInterval, std::move(func), mode);
    isAdded_ = true;
    cond_.notify_one();
    return timerId;
}

void TimerManager::cancel(TimerId id) noexcept {
    timerPool_.cancel(id);
}

void TimerManager::clear() noexcept {
    timerPool_.clear();
}

}//end namespace slark
