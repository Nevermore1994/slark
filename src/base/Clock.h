//
// Created by Nevermore on 2024/9/14.
// slark Clock
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once
#include "Time.hpp"

#include <atomic>

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

    void setSpeed(double speed) noexcept;

    [[nodiscard]] double speed() const noexcept {
        return speed_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool isPaused() const noexcept {
        return isPaused_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> pts_{0};
    std::atomic<uint64_t> lastUpdated_{0};
    std::atomic<double> speed_{1.0};
    std::atomic<bool> isPaused_{true};
    std::atomic<bool> isInited_{false};
};

} // namespace slark
