//
// Created by Nevermore on 2022/5/12.
// slark Type
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once
#include <cstdint>

namespace slark {

constexpr int64_t kInvalid = -1;
constexpr double kDEPS = 0.00000001;
constexpr float kFEPS = 0.00001f;

#define CheckIndexValid(index, vector) \
    (0 <= (index) && (static_cast<size_t>(index) < (vector).size()))

#define DoubleCompare(x, y) ((fabs(x - y) < kDEPS) ? 0 : (x < y ? -1 : 1))
#define DoubleEqual(x, y) (fabs(x - y) < kDEPS)

#define FloatCompare(x, y) ((fabs(x - y) < kFEPS) ? 0 : (x < y ? -1 : 1))
#define FloatEqual(x, y) (fabs(x - y) < kFEPS)

}
