//
// Created by Nevermore on 2021/10/22.
// slark Timer
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include <atomic>
#include <functional>
#include <unordered_map>
#include "Time.hpp"

namespace slark {

using TimerTask = std::function<void()>;

struct TimerId {
    static constexpr uint32_t kInvalidTimerId = 0;
    uint32_t id_ = kInvalidTimerId;

    TimerId() = default;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "google-explicit-constructor"
    TimerId(uint32_t t) : id_(t) {}

    operator uint32_t() const { return id_; }
#pragma clang diagnostic pop

    [[nodiscard]]  bool isValid() const noexcept { return id_ != kInvalidTimerId; }
};

struct TimerIdHasher {
    size_t operator()(const slark::TimerId& t) const noexcept {
        return std::hash<uint32_t>{}(static_cast<uint32_t>(t));
    }
};

struct TimerIdEqual {
    bool operator()(const slark::TimerId& a, const slark::TimerId& b) const noexcept {
        return a.id_ == b.id_;
    }
};

enum class ExecuteMode {
    Serial,
    Parallel
};

struct TimerInfo {
    Time::TimePoint expireTime;
    TimerId timerId{};
    ExecuteMode mode = ExecuteMode::Serial;

    bool isLoop = false;
    std::chrono::milliseconds loopInterval{};

    bool operator<(const TimerInfo& rhs) const noexcept;

    bool operator>(const TimerInfo& rhs) const noexcept;

    explicit TimerInfo(Time::TimePoint time);

    TimerInfo(const TimerInfo& info) = default;

    TimerInfo& operator=(const TimerInfo& info) = default;

    TimerInfo(TimerInfo&& info) = default;

    TimerInfo& operator=(TimerInfo&& info) = default;
};

struct TimerInfoCompare {
    bool operator()(const TimerInfo& lhs, const TimerInfo& rhs) const {
        return lhs > rhs;
    }
};

struct Timer {
    Timer();

    Timer(Time::TimePoint timePoint, TimerTask f);

    Timer(const Timer& timer) = delete;

    Timer& operator=(const Timer& timer) = delete;

    Timer(Timer&& timer) noexcept;

    Timer& operator=(Timer&& timer) noexcept;

public:
    TimerInfo timerInfo;
    TimerTask func;
    std::atomic_bool isValid = true;
};

}
