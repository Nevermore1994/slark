//
// Created by Nevermore
// slark base
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once

#include <limits>
#include <type_traits>

namespace slark {

template<typename T> requires requires (T&&) { std::is_floating_point_v<T>; }
inline bool isEqual(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon;
}

template<typename T> requires requires (T&&) { std::is_floating_point_v<T>; }
inline bool isEqualOrGreater(T a, T b, T epsilon = std::numeric_limits<T>::epsilon()) {
    return std::fabs(a - b) < epsilon || isgreater(a, b);
}

struct Range {
    uint64_t pos{};
    int64_t size{kRangeInvalid};
    
    [[nodiscard]] bool isValid() const noexcept {
        return size != kRangeInvalid;
    }

    [[nodiscard]] uint64_t end() const noexcept {
        return pos + static_cast<uint64_t>(size);
    }
private:
    static constexpr int64_t kRangeInvalid = -1;
};


}
