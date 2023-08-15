//
// Created by Nevermore on 2021/10/24.
// slark log
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <cstdio>
#include <string_view>
#include <cinttypes>

namespace slark {

enum class LogType {
    Debug = 0,
    Info,
    Warning,
    Error,
    Assert
};

static std::string_view kLogStrs[] = {" [debug] ", " [info] ", " [warning] ", " [error] ", " [assert] "};

constexpr const uint16_t kMaxLogBuffSize = 1024;

void printLog(LogType level, const char* format, ...);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#define logger(level, format, ...) \
do {                               \
        printLog(level, "[%s][Line:%d][Function:%s]" format, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); /* NOLINT(bugprone-lambda-function-name) */ \
} while(0)

#define  LogD(format, ...)  logger(LogType::Debug, format, ##__VA_ARGS__)
#define  LogI(format, ...)  logger(LogType::Info, format, ##__VA_ARGS__)
#define  LogW(format, ...)  logger(LogType::Warning, format, ##__VA_ARGS__)
#define  LogE(format, ...)  logger(LogType::Error, format, ##__VA_ARGS__)

#ifdef __clang__
#pragma clang diagnostic pop
#endif

}


