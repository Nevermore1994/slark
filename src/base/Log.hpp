//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <cinttypes>
#include <cstdio>
#include <format>
#include <print>
#include <string_view>

#include "LogConfig.h"
#include "LogOutput.h"

namespace slark {

enum class LogType {
    Print = 0,  /// only show console
    Debug = 1,
    Info,
    Warning,
    Error,
    Assert,
    Record,
};

constexpr const std::string_view kLogTags[] = {
    " [print] ", " [debug] ", " [info] ", " [warning] ", " [error] ", " [assert] ", " [record] "};

void printLog(LogType type, const std::string& log);

template <typename... Args>
void outputLog(LogType level, std::string_view format, Args&&... args) {
    auto logStr = std::vformat(format, std::make_format_args(args...));
    if (level != LogType::Print && static_cast<int>(level) >= getLogConfig().minLogLevel) {
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

#define LOGGER(level, format, ...)                                                             \
    do {                                                                                       \
        outputLog(level, "{}{}[{}][Line:{}][Function:{}]" format "\n", Time::localTimeStr(),   \
                  kLogTags[static_cast<size_t>(level)], __FILE_NAME__, __LINE__, __FUNCTION__, \
                  ##__VA_ARGS__); /* NOLINT(bugprone-lambda-function-name) */                  \
    } while (0)

// clang-format off
#define LogD(format, ...) LOGGER(LogType::Debug, format, ##__VA_ARGS__)
#define LogI(format, ...) LOGGER(LogType::Info, format, ##__VA_ARGS__)
#define LogW(format, ...) LOGGER(LogType::Warning, format, ##__VA_ARGS__)
#define LogE(format, ...) LOGGER(LogType::Error, format, ##__VA_ARGS__)
#define LogP(format, ...) LOGGER(LogType::Print, format, ##__VA_ARGS__)
#define LogR(format, ...) LOGGER(LogType::Record, format, ##__VA_ARGS__)

namespace internal {

inline bool logThrottled(uint64_t& lastUs, uint32_t intervalMs) noexcept {
    auto nowUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count());
    auto last = lastUs;
    if (nowUs - last < static_cast<uint64_t>(intervalMs) * 1000U) {
        return true;
    }
    lastUs = nowUs;
    return false;
}

}  // namespace internal

// 节流日志：同一调用点在 intervalMs 毫秒内只输出一次，用于逐帧/逐回调成功路径保留观测点
#define LogIThrottle(intervalMs, format, ...)                        \
    do {                                                             \
        static uint64_t kLastUs = 0;                                 \
        if (!slark::internal::logThrottled(kLastUs, (intervalMs))) { \
            LogI(format, ##__VA_ARGS__);                             \
        }                                                            \
    } while (0)
#define LogDThrottle(intervalMs, format, ...)                        \
    do {                                                             \
        static uint64_t kLastUs = 0;                                 \
        if (!slark::internal::logThrottled(kLastUs, (intervalMs))) { \
            LogD(format, ##__VA_ARGS__);                             \
        }                                                            \
    } while (0)

#ifdef __clang__
#pragma clang diagnostic pop
#endif
// clang-format on

}  // namespace slark
