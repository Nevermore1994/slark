//
// Created by Nevermore on 2021/12/22.
// slark Time
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include <ctime>
#include "Time.hpp"
#include "date.h"

namespace slark {

Time::TimeStamp Time::NowTimeStamp() noexcept {
    using namespace std::chrono;
    auto tp = time_point_cast<microseconds>(system_clock::now());
    tp += Time::OffsetFromUTC();
    return static_cast<TimeStamp>(tp.time_since_epoch().count());
}

std::chrono::seconds Time::OffsetFromUTC() noexcept {
    static std::chrono::seconds offset = [] {
        time_t t = time(nullptr);
        auto lt = localtime(&t);
        return std::chrono::seconds(lt->tm_gmtoff);
    }();
    return offset;
}

std::string Time::LocalTime() noexcept {
    using namespace std::chrono;
    auto tp = system_clock::now();
    tp += Time::OffsetFromUTC();
    return date::format("%F %T", tp);
}

std::string Time::LocalShortTime() noexcept {
    using namespace std::chrono;
    auto tp = time_point_cast<seconds>(system_clock::now());
    tp += Time::OffsetFromUTC();
    return date::format("%F %H:%M:%S", tp);
}
}//end namespace slark
