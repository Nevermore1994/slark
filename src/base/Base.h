//
// Created by Nevermore
// slark base
// Copyright (c) 2024 Nevermore All rights reserved.
//

#pragma once

#include <limits>
#include <cmath>

#define DISABLE_WARNINGS \
        _Pragma("clang diagnostic push") \
        _Pragma("clang diagnostic ignored \"-Weverything\"")

#define ENABLE_WARNINGS _Pragma("clang diagnostic pop")

namespace slark {

template<typename T> requires requires (T&&) { std::is_floating_point_v<T>; }
inline bool isEqual(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon;
}

template<typename T> requires requires (T&&) { std::is_floating_point_v<T>; }
inline bool isEqualOrGreater(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon || std::greater<T>()(a, b);
}

template<typename T> requires requires (T&&) { std::is_floating_point_v<T>; }
inline bool isEqualOrLess(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon || std::less<T>()(a, b);
}

template<typename T>
inline bool isGreater(T a, T b) {
    return std::greater<T>()(a, b);
}

template<typename T>
inline bool isLess(T a, T b) {
    return std::less<T>()(a, b);
}


}
