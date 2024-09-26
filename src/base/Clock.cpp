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
}

Time::TimePoint Clock::time() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (isPause_) {
        return pts_;
    }
    auto elapseTime = Time::nowTimeStamp() - lastUpdated_;
    Time::TimePoint adjustTime =  static_cast<uint64_t>(static_cast<double>(elapseTime.count) * (1.0 - speed));
    return elapseTime + pts_ - adjustTime;
}

void Clock::start() {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    pts_ = 0;
    speed = 1.0;
    isPause_ = false;
    lastUpdated_ = Time::nowTimeStamp();
}

void Clock::pause() {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    isPause_ = true;
}

void Clock::reset() {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    pts_ = 0;
    speed = 1.0;
    isPause_ = true;
}

}
