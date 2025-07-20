//
// Created by Nevermore on 2023/6/14.
// slark Assert
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include <string_view>
#include <format>

namespace slark {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#ifdef DEBUG

#define SAssert(result, format, ...)

#else

#define SAssert(result, format, ...)   /*nothing*/

#endif // if DEBUG

#ifdef __clang__
#pragma clang diagnostic pop
#endif
}
