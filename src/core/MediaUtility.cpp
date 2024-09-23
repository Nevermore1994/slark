//
// Created by Nevermore on 2022/5/27.
// slark MediaUtility
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include "MediaUtility.hpp"
#include <_types/_uint64_t.h>
#include <regex>

namespace slark {

bool isUrl(const std::string& path) {
    constexpr std::string_view kHttpRegex = "^(http|https):\\/\\/.*";
    std::regex regex(kHttpRegex.data());
    return std::regex_match(path, regex);
}

uint32_t uint32LE(const char* ptr) {
    return uint32LE(reinterpret_cast<const uint8_t*>(ptr));
}

uint32_t uint32LE(const uint8_t* ptr) {
    return static_cast<uint32_t>(ptr[3] << 24 | ptr[2] << 16 | ptr[1] << 8 | ptr[0]);
}

uint16_t uint16LE(const char* ptr) {
    return uint16LE(reinterpret_cast<const uint8_t*>(ptr));
}

uint16_t uint16LE(const uint8_t* ptr) {
    return static_cast<uint16_t>(ptr[1] << 8 | ptr[0]);
}

}//end namespace slark
