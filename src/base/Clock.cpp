//
// Created by Nevermore on 2024/9/14.
// slark Clock
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "Clock.h"
#include "Time.hpp"
#include <mutex>

namespace slark {

void Clock::setTime(Time::TimePoint count) {
    std::lock_guard<std::mutex> lock(mutex_);
    totalTime_ = count;
    start_ = Time::nowTimeStamp() - count;
}

Time::TimePoint Clock::time() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isPause_) {
        return totalTime_;
    }
    return Time::nowTimeStamp() - start_;
}

void Clock::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isPause_) {
        start_ = Time::nowTimeStamp() - totalTime_;
    } else {
        start_ = Time::nowTimeStamp();
    }
    isPause_ = false;
}

void Clock::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    isPause_ = true;
    totalTime_ = Time::nowTimeStamp() - start_;
}

void Clock::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    start_ = 0;
    totalTime_ = 0;
    isPause_ = true;
}

}
