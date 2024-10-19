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

uint32_t readUe(std::string_view buffer);
}
