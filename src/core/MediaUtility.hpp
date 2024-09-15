//
// Created by Nevermore on 2022/5/27.
// slark MediaUtility
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once

#include <cstdint>
#include "Time.hpp"
#include "AudioDefine.h"


namespace slark {

extern const std::string_view kHttpSymbol;

bool isLocalFile(const std::string& path);

uint32_t uint32LE(const char* ptr);

uint32_t uint32LE(const uint8_t* ptr);

uint16_t uint16LE(const char* ptr);

uint16_t uint16LE(const uint8_t* ptr);

Time::TimePoint toDuration(const Data& data, const Audio::AudioInfo& info);

}
