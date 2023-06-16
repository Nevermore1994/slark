//
// Created by Nevermore on 2021/12/22.
// Slark Time
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include <iomanip>
#include <sstream>
#include "Time.hpp"

namespace Slark {

Time::Timestamp Time::nowTimeStamp() {
    auto tp = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
    return tp.time_since_epoch().count();
}

std::string Time::localTime() {
    using namespace std::chrono;
    auto timePoint = system_clock::to_time_t(system_clock::now());
    auto time = std::localtime(&timePoint);
    std::stringstream ss;
    ss << std::put_time(time, "%F %T");
    return ss.str();
}
}//end namespace Slark
