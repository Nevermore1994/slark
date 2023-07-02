//
// Created by Nevermore on 2023/6/14.
// slark Assert
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once

namespace slark {

#if defined (__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#if DEBUG

void AssertMessage(bool result, const char* format, ...);

#define SAssert(result, format, ...) \
    do {                               \
        AssertMessage(result, "[assert] [%s][Line:%d][Function:%s]" format, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);  \
    } while(0)
#else

#define SAssert(result, format, ...)   /*nothing*/

#endif // if DEBUG

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#if defined (__GNUC__) && defined(__linux__)
#pragma GCC diagnostic pop
#endif
}
