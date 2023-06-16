//
// Created by Nevermore on 2022/5/27.
// Slark MediaUtility
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once

#include <string>

namespace Slark {

constexpr const char* kHttpSymbol = "https://";

bool isLocalFile(const std::string& path);

uint32_t uint32LE(const char* ptr);
uint32_t uint32LE(const uint8_t* ptr);

uint16_t uint16LE(const char* ptr);
uint16_t uint16LE(const uint8_t* ptr);

uint16_t popcount(uint64_t value);

}
