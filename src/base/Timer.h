//
// Created by Nevermore on 2021/10/22.
// slark Timer
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include "MoveOnlyFunction.hpp"
#include "Time.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace slark {

using TimerTask = MoveOnlyFunction<void()>;

struct TimerId {
    static constexpr uint64_t kInvalidTimerId = 0;
    uint64_t id_ = kInvalidTimerId;

    TimerId() = default;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "google-explicit-constructor"

    TimerId(uint64_t t) : id_(t) {}

    operator uint64_t() const {
        return id_;
    }

#pragma clang diagnostic pop

    [[nodiscard]] bool isValid() const noexcept {
        return id_ != kInvalidTimerId;
    }

    static TimerId next() noexcept {
        static std::atomic<uint64_t> counter{1};
        return TimerId{counter.fetch_add(1, std::memory_order_relaxed)};
    }
};

struct TimerEntry {
    TimerId id;
    Time::TimePoint expireTime;
    TimerTask task;
    bool isLoop = false;
    std::chrono::milliseconds loopInterval{};
    bool isValid = true;
};

} // namespace slark
