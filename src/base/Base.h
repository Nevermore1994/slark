//
// Created by Nevermore
// slark base
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once

#include <limits>
#include <type_traits>

template<typename T> requires requires (T&&) { std::is_floating_point_v<T>; }
inline bool isEqual(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon;
}

template<typename T> requires requires (T&&) { std::is_floating_point_v<T>; }
inline bool isEqualOrGreater(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon || isgreater(a, b);
}
