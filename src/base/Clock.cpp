//
// Created by Nevermore on 2024/9/14.
// slark Clock
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <mutex>
#include "Clock.h"
#include "Time.hpp"
#include "Log.hpp"

namespace slark {

void Clock::setTime(Time::TimePoint count) {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    pts_ = count;
    lastUpdated_ = Time::nowTimeStamp();
    isInited_ = true;
}

Time::TimePoint Clock::adjustSpeedTime(Time::TimePoint elapse) const {
    return static_cast<uint64_t>(static_cast<double>(elapse.count) * (1.0 - speed));
}

Time::TimePoint Clock::time() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (isPause_) {
        return pts_;
    }
    auto elapseTime = Time::nowTimeStamp() - lastUpdated_;
    return elapseTime + pts_ - adjustSpeedTime(elapseTime);
}

void Clock::start() {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (!isInited_) {
        pts_ = 0;
        speed = 1.0;
    }

    isPause_ = false;
    isInited_ = true;
    lastUpdated_ = Time::nowTimeStamp();
}

void Clock::pause() {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    isPause_ = true;
    auto elapseTime = Time::nowTimeStamp() - lastUpdated_;
    pts_ += elapseTime - adjustSpeedTime(elapseTime);
}

void Clock::reset() {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    pts_ = 0;
    speed = 1.0;
    isPause_ = true;
    isInited_ = false;
}

}
