//
// Created by Nevermore on 2022/5/27.
// slark MediaUtility
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include <regex>

namespace slark {

bool isUrl(const std::string& path) {
    constexpr std::string_view kHttpRegex = "^(http|https):\\/\\/.*";
    std::regex regex(kHttpRegex.data());
    return std::regex_match(path, regex);
}

//Golomb decode
uint32_t readUe(std::string_view buffer) {
    uint32_t zero_count = 0;

    while (!buffer.empty() && (buffer[0] & 0x80) == 0) {
        zero_count++;
        buffer.remove_prefix(1);
    }

    if (!buffer.empty()) {
        buffer.remove_prefix(1);
    }

    uint32_t value = (1 << zero_count) - 1;

    for (uint32_t i = 0; i < zero_count && !buffer.empty(); i++) {
        value = (value << 1) | (buffer[0] & 0x80);
        buffer.remove_prefix(1);
    }

    return value;
}


}//end namespace slark
