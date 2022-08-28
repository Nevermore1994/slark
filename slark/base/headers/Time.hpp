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
#include <cassert>

namespace slark{

namespace Time {

constexpr static uint64_t kInvalid = 0;

using Timestamp = uint64_t;

Time::Timestamp nowTimeStamp();
} //end namespace slark::Time


struct CTime {
private:
    constexpr static long double kTimePrecision = 0.001;
public:
    int64_t value;
    uint64_t scale;
    bool isValid;
    
    CTime()
        : value(Time::kInvalid)
        , scale(Time::kInvalid)
        , isValid(scale != Time::kInvalid) {
    }
    
    CTime(int64_t v, uint64_t s)
        : value(v)
        , scale(s)
        , isValid(scale != Time::kInvalid) {
    }
    
    CTime(double t, uint64_t s)
        : value(s * t)
        , scale(s)
        , isValid(scale != Time::kInvalid) {
    }
    
    CTime(long double t, uint64_t s)
        : value(s * t)
        , scale(s)
        , isValid(scale != Time::kInvalid) {
    }
    
    inline long double second() const noexcept {
        assert(isValid);
        if(isValid) {
            return static_cast<double>(value) / static_cast<double>(scale);
        }
        return 0;
    }
    
    inline bool operator==(const CTime& rhs) const noexcept {
        return fabs(second() - rhs.second()) < kTimePrecision;
    }
    
    inline bool operator>(const CTime& rhs) const noexcept {
        return second() - rhs.second() > kTimePrecision;
    }
    
    inline bool operator<(const CTime& rhs) const noexcept {
        return second() - rhs.second() < -kTimePrecision;
    }
    
    inline CTime operator-(const CTime& rhs) const noexcept {
        auto t = second() - rhs.second();
        return {t, scale};
    }
    
    inline CTime operator+(const CTime& rhs) const noexcept {
        auto t = second() + rhs.second();
        return {t, scale};
    }
};

struct CTimeRange {
    CTime start;
    CTime duration;
    
    CTimeRange()
        : start()
        , duration() {
        
    }
    
    CTimeRange(CTime start_, CTime duration_)
        : start(start_)
        , duration(duration_) {
        
    }
    
    inline bool operator==(const CTimeRange& rhs) const noexcept {
        if(!isValid() || !rhs.isValid()) {
            return false;
        }
        return start == rhs.start && duration == rhs.duration;
    }
    
    inline bool overlap(const CTimeRange& rhs) const noexcept {
        if(!isValid() || !rhs.isValid()) {
            return false;
        }
        auto end = start.second() + duration.second();
        auto rend = rhs.start.second() + rhs.duration.second();
        if(rend < start.second() || rhs.start.second() > end) {
            return false;
        }
        return true;
    }
    
    inline CTime end() const noexcept{
        return start + duration;
    }
    
    inline bool isValid() const noexcept {
        return start.isValid && duration.isValid;
    }
    
    inline CTimeRange overlapTimeRange(const CTimeRange& rhs) const noexcept {
        CTimeRange range;
        if(!this->overlap(rhs)) {
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


