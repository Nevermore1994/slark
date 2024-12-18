//
// Created by Nevermore on 2021/10/24.
// slark log
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <cstdio>
#include <string_view>
#include <cinttypes>
#include <format>
#include <print>
#include "LogOutput.h"

#ifdef SLARK_IOS
#include "iOSBase.h"
#endif

namespace slark {

enum class LogType {
    Print = 0, ///only show console
    Debug = 1,
    Info,
    Warning,
    Error,
    Assert,
    Record,
};

constexpr const std::string_view kLogStrs[] = {" [print] ", " [debug] ", " [info] ", " [warning] ", " [error] ", " [assert] ", " [record] "};

template <typename ...Args>
void printLog(LogType level, std::string_view format, Args&& ...args) {
    auto logStr = std::vformat(format, std::make_format_args(args...));
    if (level == LogType::Record) {
        LogOutput::shareInstance().write(logStr);
        return;
    } else if (level > LogType::Debug) {
        LogOutput::shareInstance().write(logStr);
    }
#ifdef SLARK_IOS
    outputLog(logStr);
#elif SLARK_ANDROID
#else
    std::print("{}", logStr);
    fflush(stdout);
#endif
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#define logger(level, format, ...) \
do {                               \
    printLog(level, "{}{}[{}][Line:{}][Function:{}]" format "\n", Time::localTime(), kLogStrs[static_cast<size_t>(level)], __FILE_NAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__); /* NOLINT(bugprone-lambda-function-name) */ \
} while(0)

#define  LogD(format, ...)  logger(LogType::Debug, format, ##__VA_ARGS__)
#define  LogI(format, ...)  logger(LogType::Info, format, ##__VA_ARGS__)
#define  LogW(format, ...)  logger(LogType::Warning, format, ##__VA_ARGS__)
#define  LogE(format, ...)  logger(LogType::Error, format, ##__VA_ARGS__)
#define  LogP(format, ...)  logger(LogType::Print, format, ##__VA_ARGS__)
#define  LogR(format, ...)  logger(LogType::Record, format, ##__VA_ARGS__)

#ifdef __clang__
#pragma clang diagnostic pop
#endif

}


