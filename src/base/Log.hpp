//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "LogOutput.h"

#include <cinttypes>
#include <cstdio>
#include <format>
#include <print>
#include <string_view>

namespace slark {

enum class LogType {
    Print = 0, /// only show console
    Debug = 1,
    Info,
    Warning,
    Error,
    Assert,
    Record,
};

constexpr const std::string_view kLogTags[] =
    {" [print] ", " [debug] ", " [info] ", " [warning] ", " [error] ", " [assert] ", " [record] "};

void printLog(LogType type, const std::string& log);

template <typename... Args>
void outputLog(LogType level, std::string_view format, Args&&... args) {
    auto logStr = std::vformat(format, std::make_format_args(args...));
    if (level != LogType::Print) {
        LogOutput::shareInstance().write(logStr);
    }
    if (level == LogType::Record) {
        return;
    }
#if SLAKR_IOS
    printLog(level, logStr);
#elif SLARK_ANDROID
    printLog(level, logStr);
#else
    std::print("{}", logStr);
    fflush(stdout);
#endif
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#define logger(level, format, ...)                                            \
    do {                                                                      \
        outputLog(level,                                                      \
                  "{}{}[{}][Line:{}][Function:{}]" format "\n",               \
                  Time::localTimeStr(),                                       \
                  kLogTags[static_cast<size_t>(level)],                       \
                  __FILE_NAME__,                                              \
                  __LINE__,                                                   \
                  __FUNCTION__,                                               \
                  ##__VA_ARGS__); /* NOLINT(bugprone-lambda-function-name) */ \
    } while (0)

#define LogD(format, ...) logger(LogType::Debug, format, ##__VA_ARGS__)
#define LogI(format, ...) logger(LogType::Info, format, ##__VA_ARGS__)
#define LogW(format, ...) logger(LogType::Warning, format, ##__VA_ARGS__)
#define LogE(format, ...) logger(LogType::Error, format, ##__VA_ARGS__)
#define LogP(format, ...) logger(LogType::Print, format, ##__VA_ARGS__)
#define LogR(format, ...) logger(LogType::Record, format, ##__VA_ARGS__)

#ifdef __clang__
#pragma clang diagnostic pop
#endif

} // namespace slark
