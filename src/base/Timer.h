//
// Created by Nevermore on 2021/10/22.
// slark Timer
// Copyright (c) 2021 Nevermore All rights reserved.
//


#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include "Time.hpp"

namespace slark {

using TimerTask = std::function<void()>;

using TimerId = uint32_t;

struct TimerInfo {
    Time::TimePoint expireTime;
    TimerId timerId;

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

    Timer(const Timer& timer) = default;

    Timer& operator=(const Timer& timer) = default;

    Timer(Timer&& timer) noexcept;

    Timer& operator=(Timer&& timer) noexcept;

public:
    TimerInfo timerInfo;
    TimerTask func;
    bool isValid = true;
};

}
