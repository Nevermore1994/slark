//
// Created by Nevermore on 2022/5/27.
// slark MediaUtility
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include "MediaUtility.hpp"
#include "Log.hpp"
#include <regex>

namespace slark {

bool isUrl(const std::string& path) {
    constexpr std::string_view kHttpRegex = "^(http|https):\\/\\/.*";
    std::regex regex(kHttpRegex.data());
    return std::regex_match(path, regex);
}

uint32_t uint32LE(const uint8_t* ptr) {
    return static_cast<uint32_t>(ptr[3] << 24 | ptr[2] << 16 | ptr[1] << 8 | ptr[0]);
}

uint16_t uint16LE(const uint8_t* ptr) {
    return static_cast<uint16_t>(ptr[1] << 8 | ptr[0]);
}

uint32_t uint32LE(std::string_view view) {
    SAssert(view.size() >= 4, "out of range");
    auto func = [&](int pos) {
        //This must be uint8_t for conversion, char -> uint8_t
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    return static_cast<uint32_t>(func(3) | func(2) | func(1) | func(0));
}

uint16_t uint16LE(std::string_view view) {
    SAssert(view.size() >= 2, "out of range");
    auto func = [&](int pos) {
        return static_cast<uint8_t>(view[pos]) << (pos * 8);
    };
    return static_cast<uint16_t>(func(1) | func(0));
}

}//end namespace slark
