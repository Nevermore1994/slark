//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once
#include "Time.hpp"

#include <atomic>
#include <mutex>

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

class ThreadSafeClock {
public:
    ThreadSafeClock() = default;
    ~ThreadSafeClock() = default;

    void setTime(Time::TimePoint t) noexcept {
        std::lock_guard lock(mutex_);
        clock_.setTime(t);
    }

    void keep() noexcept {
        std::lock_guard lock(mutex_);
        clock_.keep();
    }

    [[nodiscard]] Time::TimePoint time() noexcept {
        std::lock_guard lock(mutex_);
        return clock_.time();
    }

    void start() noexcept {
        std::lock_guard lock(mutex_);
        clock_.start();
    }

    void pause() noexcept {
        std::lock_guard lock(mutex_);
        clock_.pause();
    }

    void reset() noexcept {
        std::lock_guard lock(mutex_);
        clock_.reset();
    }

    void setSpeed(double speed) noexcept {
        std::lock_guard lock(mutex_);
        clock_.setSpeed(speed);
    }

    [[nodiscard]] double speed() noexcept {
        std::lock_guard lock(mutex_);
        return clock_.speed();
    }

    [[nodiscard]] bool isPaused() noexcept {
        std::lock_guard lock(mutex_);
        return clock_.isPaused();
    }

private:
    mutable std::mutex mutex_;
    Clock clock_;
};

} // namespace slark
