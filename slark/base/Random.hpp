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
    std::uniform_int_distribution<T> dis(min, max);
    return dis(gen);
}

template <typename T, typename = std::enable_if_t<std::is_floating_point_v<T>>>
T random(T min, T max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<T> dis(min, max);
    return dis(gen);
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

uint32_t shortId() noexcept;

///length 64
std::string uniqueId() noexcept;

///length 128
std::string uuid() noexcept;

}// end namespace slark::Random

