//
// Created by Nevermore on 2021/12/22.
// Slark Time
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "Time.hpp"
#include "date.h"
#include <ctime>

namespace Slark {

Time::Timestamp Time::nowTimeStamp() {
    using namespace std::chrono;
    auto tp = time_point_cast<microseconds>(system_clock::now());
    tp += Time::offsetFromUTC();
    return tp.time_since_epoch().count();
}

std::chrono::seconds Time::offsetFromUTC() {
    static std::chrono::seconds offset = [] {
        time_t t = time(nullptr);
        struct tm lt = {0};
        localtime_r(&t, &lt);
        return std::chrono::seconds(lt.tm_gmtoff);
    }();
    return offset;
}

std::string Time::localTime() {
    using namespace std::chrono;
    auto tp = system_clock::now();
    tp += Time::offsetFromUTC();
    return date::format("%F %T", tp);
}
}//end namespace Slark
