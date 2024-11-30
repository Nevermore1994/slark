//
// Created by Nevermore on 2021/12/22.
// slark Time
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include <chrono>
#include <ctime>
#include <format>
#include "Time.hpp"

namespace slark {

using std::chrono::system_clock;
using std::chrono::seconds;
using std::chrono::milliseconds;
using std::chrono::microseconds;

[[nodiscard]] std::chrono::milliseconds Time::TimePoint::toMilliSeconds() const noexcept {
    return std::chrono::milliseconds(static_cast<uint64_t>( std::round(static_cast<double>(count) / 1000.0)));
}

long double Time::TimePoint::second() const noexcept {
    return std::round(static_cast<long double>(count) / 1000.0) / 1000;
}

Time::TimePoint Time::TimePoint::operator+(std::chrono::milliseconds delta) const noexcept {
    return count + static_cast<uint64_t>(delta.count() * 1000);
}

Time::TimePoint& Time::TimePoint::operator+=(std::chrono::milliseconds delta) noexcept {
    count += static_cast<uint64_t>(delta.count() * 1000);
    return *this;
}

Time::TimePoint Time::TimePoint::operator-(std::chrono::milliseconds delta) const noexcept {
    return count - static_cast<uint64_t>(delta.count() * 1000);
}

Time::TimePoint& Time::TimePoint::operator-=(std::chrono::milliseconds delta) noexcept {
    count -= static_cast<uint64_t>(delta.count() * 1000);
    return *this;
}

Time::TimePoint Time::TimePoint::operator+(Time::TimePoint delta) const noexcept {
    return count + delta;
}

Time::TimePoint& Time::TimePoint::operator+=(Time::TimePoint delta) noexcept {
    count += delta;
    return *this;
}

Time::TimePoint Time::TimePoint::operator-(Time::TimePoint delta) const noexcept {
    return count - delta;
}

Time::TimePoint& Time::TimePoint::operator-=(Time::TimePoint delta) noexcept {
    count -= delta;
    return *this;
}

Time::TimePoint Time::nowTimeStamp() noexcept {
    auto tp = time_point_cast<microseconds>(system_clock::now());
    tp += Time::offsetFromUTC();
    return static_cast<uint64_t>(tp.time_since_epoch().count());
}

std::chrono::milliseconds Time::nowTime() noexcept {
    auto tp = time_point_cast<milliseconds>(system_clock::now());
    return tp.time_since_epoch();
}

std::chrono::seconds Time::offsetFromUTC() noexcept {
    static std::chrono::seconds offset = [] {
        time_t t = time(nullptr);
        auto lt = localtime(&t);
        return std::chrono::seconds(lt->tm_gmtoff);
    }();
    return offset;
}

std::string Time::localTime() noexcept {
    using std::chrono::system_clock;
    auto tp = system_clock::now();
    tp += Time::offsetFromUTC();
    return std::format("{}", tp);
}

std::string Time::localShortTime() noexcept {
    auto tp = time_point_cast<seconds>(system_clock::now());
    tp += Time::offsetFromUTC();
    return std::format("{:%Y-%m-%d %H:%M:%S}", tp);
}

}//end namespace slark
