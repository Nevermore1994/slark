//
// Created by Nevermore on 2024/9/14.
// slark Clock
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once
#include "Time.hpp"

#include <chrono>
#include <mutex>
#include <shared_mutex>

namespace slark {

class Clock {
public:
    Clock() = default;
    ~Clock() = default;

    void setTime(Time::TimePoint count) noexcept;

    void keep() noexcept;

    [[nodiscard]] Time::TimePoint time() noexcept;

    void start() noexcept;

    void pause() noexcept;

    void reset() noexcept;

    void setSpeed(double speed) noexcept {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        speed_ = speed;
    }

    [[nodiscard]] double speed() const noexcept {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return speed_;
    }

    [[nodiscard]] bool isPaused() const noexcept {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return isPause_;
    }

private:
    bool isInited_ = false;
    bool isPause_ = true;
    mutable std::shared_mutex mutex_;
    double speed_ = 1.0;
    Time::TimePoint lastUpdated_ = 0;
    Time::TimePoint pts_ = 0;
};

} // namespace slark
