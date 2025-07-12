//
// Created by Nevermore on 2024/9/14.
// slark Clock
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once
#include <chrono>
#include <shared_mutex>
#include "Time.hpp"

namespace slark {

class Clock {
public:
    Clock() = default;
    ~Clock() = default;

    void setTime(Time::TimePoint count) noexcept;

    ///Discard the time change since the last time
    void keep() noexcept;

    Time::TimePoint time() noexcept;

    void start() noexcept;

    void pause() noexcept;

    void reset() noexcept;

    void setSpeed(double speed) noexcept {
        speed_ = speed;
    }
private:
    bool isInited_ = false;
    bool isPause_ = true;
    std::shared_mutex mutex_;
    double speed_ = 1.0;
    Time::TimePoint lastUpdated_ = 0;
    Time::TimePoint pts_ = 0;
};

}

