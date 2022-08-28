//
// Created by Nevermore on 2021/10/22.
// slark Utility
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "Utility.hpp"
#include <chrono>
#include <thread>
#include <string>
#include <sstream>

#ifdef __linux__
#include <pthread.h>
#endif

namespace slark::Util {

using namespace std::literals;

std::string randomString(uint32_t length) {
    static std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"s;
    std::string result;
    result.resize(length);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 61);// 61 = 26 * 2 + 10 - 1. [0, 61]
    for(int i = 0; i < length; i++)
        result[i] = charset[dis(gen)];
    return result;
}

uint64_t randomId(uint8_t length) {
    if(length >= 18) {
        throw std::runtime_error("randomId error");
    }
    static std::string charset = "1234567890"s;
    std::string result;
    result.resize(length);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 9);
    for(int i = 0; i < length; i++)
        result[i] = charset[dis(gen)];
    return std::stoull(result);
}

uint32_t shortId() {
    return static_cast<uint32_t>(Util::randomId(12));
}

std::string uuid() {
    return Util::randomString(64);
}

std::vector<std::string>& spiltString(const std::string& str, const char& flag, std::vector<std::string>& res, bool isSkipSpace) {
    std::istringstream iss(str);
    for(std::string item; std::getline(iss, item, flag);) {
        if(isSkipSpace && item.empty()) {
            continue;
        } else {
            res.push_back(item);
        }
    }
    return res;
}


}//end namespace slark::Util
