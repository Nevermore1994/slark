//
// Created by Nevermore on 2023/6/14.
// Slark Random
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include <cstdint>
#include <string>
#include <random>
#include <type_traits>
#include <algorithm>

namespace Slark::Random {

//random
template<typename T>
T random(T min, T max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    if (std::is_floating_point_v<T>) {
        std::uniform_real_distribution<double> dis(min, max);
        return dis(gen);
    } else {
        std::uniform_int_distribution<T> dis(min, max);
        return dis(gen);
    }
}

template<typename T>
std::vector<T> randomSelect(const std::vector<T>& source, uint32_t count) {
    if (count >= source.size()) {
        return {source.begin(), source.end()};
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::vector<T> res(source.begin(), source.end());
    std::shuffle(res.begin(), res.end(), gen);
    return {res.begin(), res.begin() + count};
}

std::string randomString(uint32_t length);

//length 12
uint32_t shortId();

//length 18
uint64_t id();

std::string uuid();

}// end namespace Slark::Random

