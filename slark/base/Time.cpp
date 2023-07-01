//
// Created by Nevermore on 2021/12/22.
// slark Time
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "Time.hpp"
#include "date.h"
#include <ctime>

namespace slark {

Time::Timestamp Time::nowTimeStamp() noexcept {
    using namespace std::chrono;
    auto tp = time_point_cast<microseconds>(system_clock::now());
    tp += Time::offsetFromUTC();
    return tp.time_since_epoch().count();
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
    using namespace std::chrono;
    auto tp = system_clock::now();
    tp += Time::offsetFromUTC();
    return date::format("%F %T", tp);
}
}//end namespace slark
