//
// Created by Nevermore on 2021/10/22.
// slark Utility
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include <cstdint>
#include <string>
#include <random>
#include <type_traits>
#include <algorithm>

namespace slark::Util {

//random
template<typename T>
T random(T min, T max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    if(std::is_floating_point_v<T>) {
        std::uniform_real_distribution<double> dis(min, max);
        return dis(gen);
    } else {
        std::uniform_int_distribution<T> dis(min, max);
        return dis(gen);
    }
}

template<typename T>
std::vector<T> reservoirSampling(const std::vector<T>& source, uint32_t count) {
    if(count >= source.size()) {
        return {source.begin(), source.end()};
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::vector<T> res(source.begin(), source.end());
    std::shuffle(res.begin(), res.end(), gen);
    return {res.begin(), res.begin() + count};
}

std::string randomString(uint32_t length);

uint64_t randomId(uint8_t length);

//id
uint32_t shortId();

std::string uuid();

//string
std::vector<std::string>&
spiltString(const std::string& str, const char& flag, std::vector<std::string>& res, bool isSkipSpace = true);

}// end namespace slark::Util

