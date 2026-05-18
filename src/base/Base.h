//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cmath>
#include <compare>
#include <functional>
#include <limits>

#define DISABLE_WARNINGS \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Weverything\"")

#define ENABLE_WARNINGS _Pragma("clang diagnostic pop")

namespace slark {

template <typename T>
    requires std::is_floating_point_v<T>
bool isEqual(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon;
}

template <typename T>
    requires std::is_floating_point_v<T>
bool isEqualOrGreater(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon || a > b;
}

template <typename T>
    requires std::is_floating_point_v<T>
bool isEqualOrLess(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon || a < b;
}

} // namespace slark
