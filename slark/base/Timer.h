//
// Created by Nevermore on 2021/10/22.
// slark Timer
// Copyright (c) 2021 Nevermore All rights reserved.
//


#pragma once

#include <cstdint>
#include <functional>

namespace slark {

using TimerCallback = std::function<void()>;

using TimerId = uint32_t;

struct TimerInfo {
    uint64_t expireTime;
    TimerId timerId;

    bool isLoop = false;
    uint64_t loopInterval = 0; //ms

    bool operator<(const TimerInfo& rhs) const noexcept;

    bool operator>(const TimerInfo& rhs) const noexcept;

    explicit TimerInfo(uint64_t time);

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

    Timer(uint64_t timeStamp, TimerCallback f);

    Timer(const Timer& timer) = default;

    Timer& operator=(const Timer& timer) = default;

    Timer(Timer&& timer) noexcept;

    Timer& operator=(Timer&& timer) noexcept;

public:
    TimerInfo timerInfo;
    TimerCallback func;
    bool isValid = true;
};

}