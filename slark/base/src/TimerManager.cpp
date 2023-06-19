//
// Created by Nevermore on 2021/10/22.
// Slark TimerManager
// Copyright (c) 2021 Nevermore All rights reserved.
//


#include "TimerManager.hpp"
#include <chrono>

namespace Slark {
using namespace std::chrono_literals;

TimerManager::TimerManager()
    : timerThread_(std::make_unique<Thread>("TimerManager", &TimerManager::loop, this)) {
    timerThread_->start();
}

TimerManager::~TimerManager() noexcept {
    pool_.clear();
    timerThread_->stop();
}

void TimerManager::loop() {
    pool_.loop();
}

TimerId TimerManager::runAt(uint64_t timeStamp, TimerCallback func) noexcept {
    return pool_.runAt(timeStamp, std::move(func));
}

TimerId TimerManager::runAfter(uint64_t delayTime, TimerCallback func) noexcept {
    return pool_.runAfter(delayTime, std::move(func));
}

TimerId TimerManager::runLoop(uint64_t timeInterval, TimerCallback func) noexcept {
    return pool_.runLoop(timeInterval, std::move(func));
}

TimerId TimerManager::runAfter(std::chrono::milliseconds delayTime, TimerCallback func) noexcept {
    return pool_.runAfter(delayTime, std::move(func));
}

TimerId TimerManager::runLoop(std::chrono::milliseconds timeInterval, TimerCallback func) noexcept {
    return pool_.runLoop(timeInterval, std::move(func));
}

void TimerManager::cancel(TimerId id) noexcept {
    pool_.cancel(id);
}

}//end namespace Slark
