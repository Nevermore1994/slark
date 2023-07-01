//
// Created by Nevermore on 2022/5/12.
// slark Type
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once
#include <cstdint>

namespace slark {

constexpr int64_t kInvalid = -1;

#define CheckIndexValid(index, vector) \
    (0 <= (index) && ((index) < (vector).size()))


}