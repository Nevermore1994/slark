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
#include <type_traits>
#include "Assert.hpp"

namespace slark {

struct Time {
    static constexpr uint64_t kTimeInvalid = 0;
    static constexpr uint64_t kMicroSecondScale = 1000000;
    static constexpr uint64_t kMillSecondScale = 1000;
    ///microseconds
    struct TimePoint {
        uint64_t count{};

#pragma clang diagnostic push
#pragma ide diagnostic ignored "google-explicit-constructor"
        operator uint64_t() const {
            return count;
        }

        constexpr TimePoint(uint64_t t = 0)
            : count(t) {
        }

        template <typename T> requires std::is_floating_point_v<T>
        constexpr TimePoint(T time)
            : count(static_cast<uint64_t>(time * kMicroSecondScale)) {

        }

        constexpr TimePoint(std::chrono::microseconds micros)
            : count(static_cast<uint64_t>(micros.count())) {

        }

        constexpr TimePoint(std::chrono::milliseconds ms)
        : count(static_cast<uint64_t>(ms.count() * 1000)) {

        }
#pragma clang diagnostic pop
        [[nodiscard]] std::chrono::milliseconds toMilliSeconds() const noexcept;
        [[nodiscard]] long double second() const noexcept;
        TimePoint operator+(std::chrono::milliseconds delta) const noexcept;
        TimePoint& operator+=(std::chrono::milliseconds delta) noexcept;
        TimePoint operator-(std::chrono::milliseconds delta) const noexcept;
        TimePoint& operator-=(std::chrono::milliseconds delta) noexcept;
        TimePoint operator+(TimePoint delta) const noexcept;
        TimePoint& operator+=(TimePoint delta) noexcept;
        TimePoint operator-(TimePoint delta) const noexcept;
        TimePoint& operator-=(TimePoint delta) noexcept;

    };
    static TimePoint nowTimeStamp() noexcept;
    [[maybe_unused]] static std::chrono::milliseconds nowUTCTime() noexcept;
    static std::chrono::seconds offsetFromUTC() noexcept;
    static std::string localTimeStr() noexcept;
    static std::string localShortTimeStr() noexcept;
};

struct CTime {
private:
    constexpr static long double kTimePrecision = std::numeric_limits<double>::epsilon();
    constexpr static uint64_t kDefaultScale = 1000U;
public:
    int64_t value;
    uint64_t scale = kDefaultScale;

    CTime()
        : value(0)
        , scale(Time::kTimeInvalid) {
    }

    explicit CTime(int64_t value_, uint64_t scale_)
        : value(value_)
        , scale(scale_) {
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
            return std::round(static_cast<double>(value) / static_cast<double>(scale) * 1000000.0) / 1000000.0;
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

    [[nodiscard]] inline bool isOverlap(const CTimeRange& rhs) const noexcept {
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

    [[maybe_unused]] [[nodiscard]] inline CTimeRange overlap(const CTimeRange& rhs) const noexcept {
        CTimeRange range;
        if (!this->isOverlap(rhs)) {
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


