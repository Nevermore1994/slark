//
// Created by Nevermore on 2021/10/22.
// slark TimerManager
// Copyright (c) 2021 Nevermore All rights reserved.
//


#include "TimerManager.h"
#include <chrono>

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
    pool_.clear();
    timerThread_->stop();
}

void TimerManager::loop() {
    pool_.loop();
}

TimerId TimerManager::runAt(Time::TimePoint timeStamp, TimerTask func) noexcept {
    return pool_.runAt(timeStamp, std::move(func));
}

TimerId TimerManager::runAfter(std::chrono::milliseconds delayTime, TimerTask func) noexcept {
    return pool_.runAfter(delayTime, std::move(func));
}

TimerId TimerManager::runLoop(std::chrono::milliseconds timeInterval, TimerTask func) noexcept {
    return pool_.runLoop(timeInterval, std::move(func));
}

void TimerManager::cancel(TimerId id) noexcept {
    pool_.cancel(id);
}

void TimerManager::clear() noexcept {
    pool_.clear();
}

}//end namespace slark
