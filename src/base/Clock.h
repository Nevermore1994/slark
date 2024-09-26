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

    void setTime(Time::TimePoint count);

    Time::TimePoint time();

    void start();

    void pause();

    void reset();
private:
    bool isPause_ = true;
    std::shared_mutex mutex_;
    double speed = 1.0;
    Time::TimePoint lastUpdated_ = 0;
    Time::TimePoint pts_ = 0;
};

}

