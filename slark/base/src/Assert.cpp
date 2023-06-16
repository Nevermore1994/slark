//
// Created by Nevermore on 2023/6/14.
// slark Assert
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include <cstdarg>
#include <stdexcept>
#include "Assert.hpp"

#if DEBUG
void AssertMessage(bool result, const char* format, ...) {
    if (!result) {
        constexpr int kMaxMessageBuffSize = 512;
        char buf[kMaxMessageBuffSize] = {0};
        va_list args;
        va_start(args, format);
        vsnprintf((char*)(buf), (kMaxMessageBuffSize - 1), format, args);
        va_end(args);
        buf[kMaxMessageBuffSize - 1] = 0;
        throw std::runtime_error(buf);
    }
}

#endif
