//
// Created by Nevermore on 2021/12/22.
// slark Time
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "Time.hpp"

#include <chrono>
#include <ctime>
#include <format>

namespace slark {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;
using std::chrono::time_point_cast;

Time::TimePoint Time::nowTimeStamp() noexcept {
    // 使用单调时钟，不受 NTP/时区跳变影响
    auto tp = time_point_cast<microseconds>(std::chrono::steady_clock::now());
    return static_cast<uint64_t>(tp.time_since_epoch().count());
}

std::chrono::milliseconds Time::nowUTCTime() noexcept {
    return time_point_cast<milliseconds>(system_clock::now()).time_since_epoch();
}

std::chrono::seconds Time::offsetFromUTC() noexcept {
    static auto offset = [] {
        auto t = std::time(nullptr);
        return seconds(std::localtime(&t)->tm_gmtoff);
    }();
    return offset;
}

std::string Time::localTimeStr() noexcept {
    auto tp = system_clock::now() + offsetFromUTC();
    return std::format("{}", tp);
}

std::string Time::localShortTimeStr() noexcept {
    auto tp = time_point_cast<seconds>(system_clock::now()) + offsetFromUTC();
    return std::format("{:%Y-%m-%d %H:%M:%S}", tp);
}

} // end namespace slark
