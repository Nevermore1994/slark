//
// Created by Nevermore on 2021/10/22.
// slark Utility
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "Random.hpp"

#ifdef __linux__
    #include <pthread.h>
#endif

namespace slark::Random {

using namespace std::literals;
constexpr static std::string_view kRandomString = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"sv;

std::string randomString(uint32_t length) noexcept {
    std::string result;
    result.resize(length);

    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<> dis(0, static_cast<int>(kRandomString.size()));// 61 = 26 * 2 + 10 - 1. [0, 61]
    for (uint32_t i = 0; i < length; i++) {
        result[i] = kRandomString[static_cast<uint32_t>(dis(gen))];
    }
    return result;
}

uint32_t u32Random() noexcept {
    return static_cast<uint32_t>(Random::random<uint32_t>(0, UINT32_MAX));
}

int32_t i32Random() noexcept {
    return static_cast<int32_t>(Random::random<int32_t>(0, INT32_MAX));
}

std::string uniqueId() noexcept {
    return randomString(64);
}

std::string uuid() noexcept {
    std::string result;
    result.reserve(31);

    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<> dis(0, static_cast<int>(kRandomString.size() - 1));

    auto appendRandomChars = [&](size_t length) {
        for (size_t i = 0; i < length; ++i) {
            result.push_back(kRandomString[static_cast<unsigned long>(dis(gen))]);
        }
    };
    appendRandomChars(8); result.push_back('-');
    appendRandomChars(4); result.push_back('-');
    appendRandomChars(4); result.push_back('-');
    appendRandomChars(4); result.push_back('-');
    appendRandomChars(12);
    return result;
}

}//end namespace slark::Random
