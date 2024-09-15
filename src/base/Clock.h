//
// Created by Nevermore on 2024/9/14.
// slark Clock
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once
#include <chrono>
#include <mutex>
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
    std::mutex mutex_;
    Time::TimePoint start_ = 0;
    Time::TimePoint totalTime_ = 0;
};

}

