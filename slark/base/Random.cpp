//
// Created by Nevermore on 2021/10/22.
// Slark Utility
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

namespace Slark::Random {

using namespace std::literals;

std::string randomString(uint32_t length) {
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

uint32_t shortId() {
    return static_cast<uint32_t>(Random::random<uint32_t>(0, UINT32_MAX));
}

uint64_t id() {
    return static_cast<uint32_t>(Random::random<uint64_t>(0, UINT64_MAX));
}

std::string uuid() {
    return randomString(64);
}


}//end namespace Slark::Random
