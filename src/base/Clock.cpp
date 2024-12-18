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

void Clock::setTime(Time::TimePoint count) noexcept {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    pts_ = count;
    lastUpdated_ = Time::nowTimeStamp();
    isInited_ = true;
}

Time::TimePoint Clock::adjustSpeedTime(Time::TimePoint elapse) const noexcept {
    return static_cast<uint64_t>(static_cast<double>(elapse.count) * (1.0 - speed));
}

Time::TimePoint Clock::time() noexcept {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (isPause_) {
        return pts_;
    }
    auto elapseTime = Time::nowTimeStamp() - lastUpdated_;
    return elapseTime + pts_ - adjustSpeedTime(elapseTime);
}

void Clock::start() noexcept {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (!isInited_) {
        pts_ = 0;
        speed = 1.0;
    }

    isPause_ = false;
    isInited_ = true;
    lastUpdated_ = Time::nowTimeStamp();
}

void Clock::pause() noexcept {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    if (!isInited_) {
        return;
    }
    isPause_ = true;
    auto elapseTime = Time::nowTimeStamp() - lastUpdated_;
    pts_ += elapseTime - adjustSpeedTime(elapseTime);
}

void Clock::reset() noexcept {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    pts_ = 0;
    speed = 1.0;
    isPause_ = true;
    isInited_ = false;
}

}
