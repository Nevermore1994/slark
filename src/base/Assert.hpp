//
// Created by Nevermore on 2023/6/14.
// slark Assert
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include <format>
#include <print>
#include <string_view>

namespace slark {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#ifdef DEBUG

#define SAssert(result, format, ...)                       \
    do {                                                   \
        if (!(result)) [[unlikely]] {                      \
            std::print(stderr,                             \
                       "[Assert] {}:{} ({}) " format "\n", \
                       __FILE_NAME__,                      \
                       __LINE__,                           \
                       __FUNCTION__,                       \
                       ##__VA_ARGS__);                     \
            __builtin_trap();                              \
        }                                                  \
    } while (0)

#else

#define SAssert(result, format, ...) /*nothing*/

#endif // DEBUG

#ifdef __clang__
#pragma clang diagnostic pop
#endif

} // namespace slark
