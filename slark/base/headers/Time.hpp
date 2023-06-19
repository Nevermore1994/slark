//
// Created by Nevermore on 2021/12/22.
// Slark Time
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <chrono>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>
#include "Assert.hpp"

namespace Slark {

namespace Time {

constexpr static uint64_t kTimeInvalid = 0;

using Timestamp = uint64_t;

std::chrono::seconds offsetFromUTC();

std::string localTime();

//system time
Time::Timestamp nowTimeStamp();

} //end namespace Slark::Time


struct CTime {
private:
    constexpr static long double kTimePrecision = 0.001;
public:
    int64_t value;
    uint64_t scale;

    CTime()
        : value(Time::kTimeInvalid)
        , scale(Time::kTimeInvalid) {
    }

    CTime(int64_t v, uint64_t s)
        : value(v)
        , scale(s) {
    }

    CTime(long double t, uint64_t s)
        : value(s * t)
        , scale(s) {
    }

    [[nodiscard]] inline long double second() const noexcept {
        bool isValid = this->isValid();
        AssertMessage(isValid, "time is invalid");
        if (isValid) {
            return static_cast<double>(value) / static_cast<double>(scale);
        }
        return 0;
    }

    inline bool operator==(const CTime& rhs) const noexcept {
        return fabs(second() - rhs.second()) < kTimePrecision;
    }

    inline bool operator>(const CTime& rhs) const noexcept {
        return (second() - rhs.second()) > kTimePrecision;
    }

    inline bool operator<(const CTime& rhs) const noexcept {
        return (second() - rhs.second()) < -kTimePrecision;
    }

    inline bool operator>=(const CTime& rhs) const noexcept {
        return this->operator==(rhs) || this->operator>(rhs);
    }

    inline bool operator<=(const CTime& rhs) const noexcept {
        return this->operator==(rhs) || this->operator<(rhs);
    }

    inline CTime operator-(const CTime& rhs) const noexcept {
        auto t = second() - rhs.second();
        return {t, scale};
    }

    inline CTime operator+(const CTime& rhs) const noexcept {
        auto t = second() + rhs.second();
        return {t, scale};
    }

    [[nodiscard]] inline bool isValid() const noexcept {
        return scale != Time::kTimeInvalid;
    }
};

struct CTimeRange {
    CTime start;
    CTime duration;

    CTimeRange()
        : start()
        , duration() {

    }

    [[maybe_unused]] CTimeRange(CTime start_, CTime duration_)
        : start(start_)
        , duration(duration_) {

    }

    [[nodiscard]] inline bool operator==(const CTimeRange& rhs) const noexcept {
        if (!isValid() || !rhs.isValid()) {
            return false;
        }
        return start == rhs.start && duration == rhs.duration;
    }

    [[nodiscard]] inline bool overlap(const CTimeRange& rhs) const noexcept {
        if (!isValid() || !rhs.isValid()) {
            return false;
        }
        auto end = start.second() + duration.second();
        auto rend = rhs.start.second() + rhs.duration.second();
        if (rend < start.second() || rhs.start.second() > end) {
            return false;
        }
        return true;
    }

    [[nodiscard]] inline CTime end() const noexcept {
        return start + duration;
    }

    [[nodiscard]] inline bool isValid() const noexcept {
        return start.isValid() && duration.isValid();
    }

    [[maybe_unused]] [[nodiscard]] inline CTimeRange overlapRange(const CTimeRange& rhs) const noexcept {
        CTimeRange range;
        if (!this->overlap(rhs)) {
            return range;
        }
        auto end = this->end().second();
        auto rend = rhs.end().second();
        range.start = std::max(start, rhs.start);
        range.duration = CTime(std::min(end, rend), start.scale) - range.start;
        return range;
    }
};

}


