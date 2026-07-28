//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstdint>
#include <string>
#include <type_traits>

#include "Assert.hpp"

namespace slark::Time {

constexpr uint64_t kTimeInvalid = 0;
constexpr uint64_t kMicroSecondScale = 1000000;
constexpr uint64_t kMillSecondScale = 1000;

class TimeDelta {
 private:
    int64_t microseconds_{};

 public:
    explicit(false) TimeDelta(int64_t us = 0) noexcept : microseconds_(us) {}

    [[nodiscard]] int64_t point() const noexcept {
        return microseconds_;
    }

    [[nodiscard]] std::chrono::milliseconds toMilliSeconds() const noexcept {
        return std::chrono::milliseconds(microseconds_ / 1000);
    }

    [[nodiscard]] double second() const noexcept {
        return static_cast<double>(microseconds_) / kMicroSecondScale;
    }

    [[nodiscard]] double millisecond() const noexcept {
        return static_cast<double>(microseconds_) / kMillSecondScale;
    }

    static TimeDelta fromMilliSeconds(std::chrono::milliseconds ms) noexcept {
        return TimeDelta(ms.count() * 1000);
    }

    static TimeDelta fromMicroSeconds(std::chrono::microseconds us) noexcept {
        return TimeDelta(us.count());
    }

    template <typename T>
        requires std::is_floating_point_v<T>
    static TimeDelta fromSeconds(T seconds) noexcept {
        return TimeDelta(static_cast<int64_t>(seconds * kMicroSecondScale));
    }

    TimeDelta operator+(const TimeDelta& rhs) const noexcept {
        return TimeDelta(microseconds_ + rhs.microseconds_);
    }

    TimeDelta& operator+=(const TimeDelta& rhs) noexcept {
        microseconds_ += rhs.microseconds_;
        return *this;
    }

    TimeDelta operator-(const TimeDelta& rhs) const noexcept {
        return TimeDelta(microseconds_ - rhs.microseconds_);
    }

    TimeDelta& operator-=(const TimeDelta& rhs) noexcept {
        microseconds_ -= rhs.microseconds_;
        return *this;
    }

    auto operator<=>(const TimeDelta& rhs) const noexcept = default;
};

class TimePoint {
 private:
    uint64_t microseconds_{};

 public:
    explicit(false) constexpr TimePoint(uint64_t t = 0) noexcept : microseconds_(t) {}

    [[nodiscard]] uint64_t point() const noexcept {
        return microseconds_;
    }

    [[nodiscard]] std::chrono::milliseconds toMilliSeconds() const noexcept {
        return std::chrono::milliseconds(microseconds_ / 1000);
    }

    [[nodiscard]] double millisecond() const noexcept {
        return static_cast<double>(microseconds_) / kMillSecondScale;
    }

    [[nodiscard]] double second() const noexcept {
        return static_cast<double>(microseconds_) / kMicroSecondScale;
    }

    static TimePoint fromMilliSeconds(std::chrono::milliseconds ms) noexcept {
        return TimePoint(static_cast<uint64_t>(ms.count() * 1000));
    }

    static TimePoint fromMicroSeconds(std::chrono::microseconds micros) noexcept {
        return TimePoint(static_cast<uint64_t>(micros.count()));
    }

    template <typename T>
        requires std::is_floating_point_v<T>
    static TimePoint fromSeconds(T seconds) noexcept {
        return TimePoint(static_cast<uint64_t>(seconds * kMicroSecondScale));
    }

    auto operator<=>(const TimePoint& rhs) const noexcept = default;
    bool operator==(const TimePoint& rhs) const noexcept = default;

    auto operator<=>(std::chrono::milliseconds ms) const noexcept {
        return *this <=> TimePoint::fromMilliSeconds(ms);
    }

    bool operator==(std::chrono::milliseconds ms) const noexcept {
        return *this == TimePoint::fromMilliSeconds(ms);
    }

    TimePoint operator+(const TimeDelta& delta) const noexcept {
        return TimePoint(microseconds_ + static_cast<uint64_t>(delta.point()));
    }

    TimePoint& operator+=(const TimeDelta& delta) noexcept {
        microseconds_ += static_cast<uint64_t>(delta.point());
        return *this;
    }

    TimePoint operator-(const TimeDelta& delta) const noexcept {
        return TimePoint(microseconds_ - static_cast<uint64_t>(delta.point()));
    }

    TimePoint& operator-=(const TimeDelta& delta) noexcept {
        microseconds_ -= static_cast<uint64_t>(delta.point());
        return *this;
    }

    TimeDelta operator-(const TimePoint& other) const noexcept {
        return TimeDelta(static_cast<int64_t>(microseconds_) -
                         static_cast<int64_t>(other.microseconds_));
    }

    TimePoint operator+(std::chrono::milliseconds ms) const noexcept {
        return *this + TimeDelta::fromMilliSeconds(ms);
    }

    TimePoint& operator+=(std::chrono::milliseconds ms) noexcept {
        return *this += TimeDelta::fromMilliSeconds(ms);
    }

    TimeDelta operator-(std::chrono::milliseconds ms) const noexcept {
        return *this - TimePoint::fromMilliSeconds(ms);
    }

    TimePoint operator+(TimePoint other) const noexcept {
        return TimePoint(microseconds_ + other.microseconds_);
    }

    TimePoint& operator+=(TimePoint other) noexcept {
        microseconds_ += other.microseconds_;
        return *this;
    }
};

TimePoint nowTimeStamp() noexcept;
std::chrono::milliseconds nowUTCTime() noexcept;
std::chrono::seconds offsetFromUTC() noexcept;
std::string localTimeStr() noexcept;
std::string localShortTimeStr() noexcept;

class Deadline {
 public:
    explicit Deadline(std::chrono::milliseconds timeout) noexcept
        : expirePoint_(nowTimeStamp() + timeout) {}

    [[nodiscard]] bool isExpired() const noexcept {
        return nowTimeStamp() >= expirePoint_;
    }

    [[nodiscard]] std::chrono::milliseconds remaining() const noexcept {
        auto now = nowTimeStamp();
        if (now >= expirePoint_) {
            return std::chrono::milliseconds(0);
        }
        return (expirePoint_ - now).toMilliSeconds();
    }

 private:
    TimePoint expirePoint_;
};

struct CTime {
 private:
    static constexpr double kTimePrecision = std::numeric_limits<double>::epsilon();
    static constexpr uint64_t kDefaultScale = 1000U;

 public:
    int64_t value;
    uint64_t scale = kDefaultScale;

    CTime() : value(0), scale(kTimeInvalid) {}

    explicit CTime(int64_t v, uint64_t s) : value(v), scale(s) {}

    explicit CTime(double t)
        : value(static_cast<int64_t>(kDefaultScale * t)), scale(kDefaultScale) {}

    [[nodiscard]] double second() const noexcept {
        bool valid = isValid();
        SAssert(valid, "time is invalid");
        if (valid) {
            return std::round(static_cast<double>(value) / static_cast<double>(scale) * 1000000.0) /
                   1000000.0;
        }
        return 0;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return scale != kTimeInvalid;
    }

    bool operator==(const CTime& rhs) const noexcept {
        return std::fabs(second() - rhs.second()) < kTimePrecision;
    }

    std::partial_ordering operator<=>(const CTime& rhs) const noexcept {
        auto diff = second() - rhs.second();
        if (std::fabs(diff) < kTimePrecision) {
            return std::partial_ordering::equivalent;
        }
        return diff > 0 ? std::partial_ordering::greater : std::partial_ordering::less;
    }

    CTime operator-(const CTime& rhs) const noexcept {
        auto t = second() - rhs.second();
        return CTime(static_cast<int64_t>(t * static_cast<double>(scale)), scale);
    }

    CTime operator+(const CTime& rhs) const noexcept {
        auto t = second() + rhs.second();
        return CTime(static_cast<int64_t>(t * static_cast<double>(scale)), scale);
    }
};

struct CTimeRange {
    CTime start;
    CTime duration;

    CTimeRange() = default;

    CTimeRange(CTime s, CTime d) : start(s), duration(d) {}

    [[nodiscard]] CTime end() const noexcept {
        return start + duration;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return start.isValid() && duration.isValid();
    }

    bool operator==(const CTimeRange& rhs) const noexcept {
        SAssert(isValid() && rhs.isValid(), "time is invalid");
        if (!isValid() || !rhs.isValid()) {
            return false;
        }
        return start == rhs.start && duration == rhs.duration;
    }

    std::partial_ordering operator<=>(const CTimeRange& rhs) const noexcept {
        SAssert(isValid() && rhs.isValid(), "time is invalid");
        if (!isValid() || !rhs.isValid()) {
            return std::partial_ordering::unordered;
        }
        if (end() < rhs.start) {
            return std::partial_ordering::less;
        }
        if (end() > rhs.start) {
            return std::partial_ordering::greater;
        }
        return std::partial_ordering::equivalent;
    }

    [[nodiscard]] bool isOverlap(const CTimeRange& rhs) const noexcept {
        if (!isValid() || !rhs.isValid()) {
            return false;
        }
        return start.second() < rhs.end().second() && rhs.start.second() < end().second();
    }

    [[nodiscard]] CTimeRange overlap(const CTimeRange& rhs) const noexcept {
        if (!isOverlap(rhs)) {
            return {};
        }
        auto overlapStart = std::max(start, rhs.start);
        auto overlapEnd = CTime(std::min(end().second(), rhs.end().second()));
        return {overlapStart, overlapEnd - overlapStart};
    }
};

}  // namespace slark::Time

namespace slark {
using Time::CTime;
using Time::CTimeRange;
using Time::Deadline;
}  // namespace slark
