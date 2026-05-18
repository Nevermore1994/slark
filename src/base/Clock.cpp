//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Clock.h"

#include "Time.hpp"

namespace slark {

void Clock::setTime(Time::TimePoint count) noexcept {
    pts_.store(count.point(), std::memory_order_relaxed);
    lastUpdated_.store(Time::nowTimeStamp().point(), std::memory_order_relaxed);
    isInited_.store(true, std::memory_order_relaxed);
}

void Clock::keep() noexcept {
    lastUpdated_.store(Time::nowTimeStamp().point(), std::memory_order_relaxed);
    isInited_.store(true, std::memory_order_relaxed);
}

Time::TimePoint Clock::time() noexcept {
    if (isPaused_.load(std::memory_order_relaxed)) {
        return Time::TimePoint(pts_.load(std::memory_order_relaxed));
    }
    auto pts = pts_.load(std::memory_order_relaxed);
    auto last = lastUpdated_.load(std::memory_order_relaxed);
    auto now = Time::nowTimeStamp().point();
    auto elapsed = now > last ? (now - last) : 0;
    auto speed = speed_.load(std::memory_order_relaxed);
    return Time::TimePoint(pts + static_cast<uint64_t>(static_cast<double>(elapsed) * speed));
}

void Clock::start() noexcept {
    if (!isInited_.load(std::memory_order_relaxed)) {
        pts_.store(0, std::memory_order_relaxed);
        speed_.store(1.0, std::memory_order_relaxed);
    }
    lastUpdated_.store(Time::nowTimeStamp().point(), std::memory_order_relaxed);
    isPaused_.store(false, std::memory_order_relaxed);
    isInited_.store(true, std::memory_order_relaxed);
}

void Clock::pause() noexcept {
    if (!isInited_.load(std::memory_order_relaxed)) {
        return;
    }
    // 冻结当前 pts，避免 pause 后 time() 继续推进
    auto now = Time::nowTimeStamp().point();
    auto last = lastUpdated_.load(std::memory_order_relaxed);
    auto pts = pts_.load(std::memory_order_relaxed);
    auto speed = speed_.load(std::memory_order_relaxed);
    auto elapsed = now > last ? (now - last) : 0;
    auto newPts = pts + static_cast<uint64_t>(static_cast<double>(elapsed) * speed);
    pts_.store(newPts, std::memory_order_relaxed);
    lastUpdated_.store(now, std::memory_order_relaxed);
    isPaused_.store(true, std::memory_order_relaxed);
}

void Clock::reset() noexcept {
    pts_.store(0, std::memory_order_relaxed);
    speed_.store(1.0, std::memory_order_relaxed);
    isPaused_.store(true, std::memory_order_relaxed);
    isInited_.store(false, std::memory_order_relaxed);
}

void Clock::setSpeed(double speed) noexcept {
    if (!isPaused_.load(std::memory_order_relaxed)) {
        // 运行中：先冻结当前 pts，再改 speed，避免速度突变导致时间跳变
        auto now = Time::nowTimeStamp().point();
        auto last = lastUpdated_.load(std::memory_order_relaxed);
        auto pts = pts_.load(std::memory_order_relaxed);
        auto oldSpeed = speed_.load(std::memory_order_relaxed);
        auto elapsed = now > last ? (now - last) : 0;
        auto newPts = pts + static_cast<uint64_t>(static_cast<double>(elapsed) * oldSpeed);
        pts_.store(newPts, std::memory_order_relaxed);
        lastUpdated_.store(now, std::memory_order_relaxed);
    }
    speed_.store(speed, std::memory_order_relaxed);
}

} // namespace slark
