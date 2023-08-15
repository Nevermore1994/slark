//
// Created by Nevermore on 2021/10/22.
// slark Utility
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "Random.hpp"
#include <chrono>
#include <thread>
#include <string>
#include <sstream>

#ifdef __linux__
    #include <pthread.h>
#endif

namespace slark::Random {

using namespace std::literals;

std::string randomString(uint32_t length) noexcept {
    const static std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"s;
    std::string result;
    result.resize(length);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 61);// 61 = 26 * 2 + 10 - 1. [0, 61]
    for (uint32_t i = 0; i < length; i++) {
        result[i] = charset[static_cast<uint32_t>(dis(gen))];
    }
    return result;
}

uint32_t shortId() noexcept {
    return static_cast<uint32_t>(Random::random<uint32_t>(0, UINT32_MAX));
}

std::string uniqueId() noexcept {
    return randomString(64);
}

std::string uuid() noexcept {
    return randomString(128);
}


}//end namespace slark::Random
