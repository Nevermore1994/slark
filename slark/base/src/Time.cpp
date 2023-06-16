//
// Created by Nevermore on 2021/12/22.
// Slark Time
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include <string>
#include "Time.hpp"
#include "date.h"

namespace Slark {

Time::Timestamp Time::nowTimeStamp() {
    using namespace std::chrono;
    auto tp = time_point_cast<microseconds>(system_clock::now());
    tp += 8h;
    return tp.time_since_epoch().count();
}

std::string Time::localTime() {
    using namespace std::chrono;
    auto tp = system_clock::now();
    tp += 8h;
    return date::format("%F %T", tp);
}
}//end namespace Slark
