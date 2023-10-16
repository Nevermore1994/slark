//
// Created by Nevermore on 2022/5/27.
// slark MediaUtility
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include "MediaUtility.hpp"


namespace slark {

bool isLocalFile(const std::string& path) {
    auto pos = path.find(kHttpSymbol);
    return pos == std::string::npos;
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
