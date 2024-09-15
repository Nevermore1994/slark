//
// Created by Nevermore on 2023/6/14.
// slark Random
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include <cstdint>
#include <string>
#include <random>
#include <type_traits>
#include <algorithm>

namespace slark::Random {

//random
template <typename T>
T random(T min, T max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    if constexpr (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<T> dis(min, max);
        return dis(gen);
    } else {
        std::uniform_int_distribution<T> dis(min, max);
        return dis(gen);
    }
}

template<typename T>
std::vector<T> randomSelect(const std::vector<T>& source, uint32_t count) noexcept {
    if (count >= source.size()) {
        return {source.begin(), source.end()};
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::vector<T> res(source.begin(), source.end());
    std::shuffle(res.begin(), res.end(), gen);
    return {res.begin(), res.begin() + count};
}

std::string randomString(uint32_t length) noexcept;

uint32_t u32Random() noexcept;

int32_t i32Random() noexcept;

///length 64 random str
std::string uniqueId() noexcept;

std::string uuid() noexcept;

}// end namespace slark::Random

