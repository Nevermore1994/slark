//
// Created by Nevermore on 2021/10/22.
// slark Timer
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "Timer.h"
#include "Random.hpp"
#include "Time.hpp"

namespace slark {

TimerInfo::TimerInfo(Time::TimePoint time)
    : expireTime(time)
    , timerId(Random::u32Random()) {

}

bool TimerInfo::operator<(const TimerInfo& rhs) const noexcept {
    return expireTime < rhs.expireTime;
}

bool TimerInfo::operator>(const TimerInfo& rhs) const noexcept {
    return expireTime > rhs.expireTime;
}

Timer::Timer()
    : timerInfo(0)
    , func(nullptr)
    , isValid(false) {

}

Timer::Timer(Time::TimePoint timePoint, TimerTask f)
    : timerInfo(timePoint)
    , func(std::move(f)) {

}

Timer::Timer(Timer&& timer) noexcept
    : timerInfo(timer.timerInfo)
    , func(std::move(timer.func))
    , isValid(timer.isValid) {

}

Timer& Timer::operator=(Timer&& timer) noexcept {
    this->timerInfo = timer.timerInfo;
    this->isValid = timer.isValid;
    this->func = std::move(timer.func);
    return *this;
}

}//end namespace slark
