//
// Created by Nevermore on 2022/5/12.
// Slark Type
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once
#include <cstdint>

namespace Slark {

constexpr int32_t kInvalid = -1;

#define CheckIndexValid(index, vector) \
    (0 <= (index) && ((index) < (vector).size()))


}