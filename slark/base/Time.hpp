//
// Created by Nevermore on 2021/12/22.
// slark Time
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <chrono>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>
#include "Assert.hpp"

namespace slark {

namespace Time {

constexpr static uint64_t kTimeInvalid = 0;

using TimeStamp = uint64_t;

std::chrono::seconds OffsetFromUTC() noexcept;

std::string LocalTime() noexcept;

std::string LocalShortTime() noexcept;

//system time
Time::TimeStamp NowTimeStamp() noexcept;

} //end namespace slark::Time


struct CTime {
private:
    constexpr static long double kTimePrecision = 0.001;
    constexpr static uint64_t kDefaultScale = 1000U;
public:
    int64_t value;
    uint64_t scale = 1000;

    CTime()
        : value(0)
        , scale(Time::kTimeInvalid) {
    }

    explicit CTime(int64_t v, uint64_t s)
        : value(v)
        , scale(s) {
    }

    /// init with seconds
    explicit CTime(long double t)
        : value(static_cast<int64_t>(kDefaultScale * t))
        , scale(kDefaultScale) {
    }

    [[nodiscard]] inline long double second() const noexcept {
        bool isValid = this->isValid();
        SAssert(isValid, "time is invalid");
        if (isValid) {
            return static_cast<double>(value) / static_cast<double>(scale);
        }
        return 0;
    }

    inline bool operator==(const CTime& rhs) const noexcept {
        return fabsl(second() - rhs.second()) < kTimePrecision;
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
        return CTime(static_cast<int64_t>(t * static_cast<long double>(scale)), scale);
    }

    inline CTime operator+(const CTime& rhs) const noexcept {
        auto t = second() + rhs.second();
        return CTime(static_cast<int64_t>(t * static_cast<long double>(scale)), scale);
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
        SAssert(isValid() && rhs.isValid(), "time is invalid");
        if (!isValid() || !rhs.isValid()) {
            return false;
        }
        return start == rhs.start && duration == rhs.duration;
    }

    [[nodiscard]] inline bool operator<(const CTimeRange& rhs) const noexcept {
        SAssert(isValid() && rhs.isValid(), "time is invalid");
        if (!isValid() || !rhs.isValid()) {
            return false;
        }
        return end() < rhs.start;
    }

    [[nodiscard]] inline bool operator>(const CTimeRange& rhs) const noexcept {
        SAssert(isValid() && rhs.isValid(), "time is invalid");
        if (!isValid() || !rhs.isValid()) {
            return false;
        }
        return end() > rhs.start;
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
        range.duration = CTime(static_cast<int64_t>(std::min(end, rend)), start.scale) - range.start;
        return range;
    }
};

}


