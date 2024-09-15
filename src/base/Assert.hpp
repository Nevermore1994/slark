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

#if DEBUG

template <typename ...Args>
void AssertMessage(bool result, std::string_view format, Args&& ...args){
    if (result) {
        return;
    }
    throw std::runtime_error(std::vformat(format, std::make_format_args(args...)));
}

#define SAssert(result, format, ...) \
    do {                               \
        AssertMessage(result, "[assert] [{}][Line:{}][Function:{}]" format, __FILE_NAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__);  \
    } while(0)
#else

#define SAssert(result, format, ...)   /*nothing*/

#endif // if DEBUG

#ifdef __clang__
#pragma clang diagnostic pop
#endif
}
