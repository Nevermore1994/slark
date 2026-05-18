//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Time.hpp"

#include <chrono>
#include <ctime>
#include <format>

namespace slark::Time {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;
using std::chrono::time_point_cast;

TimePoint nowTimeStamp() noexcept {
    auto tp = time_point_cast<microseconds>(std::chrono::steady_clock::now());
    return static_cast<uint64_t>(tp.time_since_epoch().count());
}

milliseconds nowUTCTime() noexcept {
    return time_point_cast<milliseconds>(system_clock::now()).time_since_epoch();
}

seconds offsetFromUTC() noexcept {
    static auto offset = [] {
        auto t = std::time(nullptr);
        return seconds(std::localtime(&t)->tm_gmtoff);
    }();
    return offset;
}

std::string localTimeStr() noexcept {
    auto tp = system_clock::now() + offsetFromUTC();
    return std::format("{}", tp);
}

std::string localShortTimeStr() noexcept {
    auto tp = time_point_cast<seconds>(system_clock::now()) + offsetFromUTC();
    return std::format("{:%Y-%m-%d %H:%M:%S}", tp);
}

} // namespace slark::Time
