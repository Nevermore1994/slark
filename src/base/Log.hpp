//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <format>
#include <print>
#include <string>
#include <string_view>

#include "LogConfig.h"

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

namespace Time {
std::string localTimeStr() noexcept;
}

/// Output a log: writes to file if it meets the minimum level threshold, then prints per platform
void outputLog(LogType level, const std::string& log);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#define LOGGER(level, fmt, ...)                                                                  \
    do {                                                                                         \
        outputLog(level, std::format("{}{}[{}][Line:{}][Function:{}]" fmt "\n",                  \
                                     Time::localTimeStr(), kLogTags[static_cast<size_t>(level)], \
                                     __FILE_NAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__));     \
    } while (0)

// clang-format off
#define LogD(format, ...) LOGGER(LogType::Debug, format, ##__VA_ARGS__)
#define LogI(format, ...) LOGGER(LogType::Info, format, ##__VA_ARGS__)
#define LogW(format, ...) LOGGER(LogType::Warning, format, ##__VA_ARGS__)
#define LogE(format, ...) LOGGER(LogType::Error, format, ##__VA_ARGS__)
#define LogP(format, ...) LOGGER(LogType::Print, format, ##__VA_ARGS__)
#define LogR(format, ...) LOGGER(LogType::Record, format, ##__VA_ARGS__)

#ifdef DEBUG
#define LogA(condition, fmt, ...)                                                          \
    do {                                                                                   \
        if (!(condition)) [[unlikely]] {                                                   \
            LOGGER(LogType::Assert, fmt, ##__VA_ARGS__);                                   \
            __builtin_trap();                                                              \
        }                                                                                  \
    } while (0)
#else
#define LogA(condition, format, ...) /*nothing*/
#endif

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

// Throttled log: output at most once per intervalMs at the same call site,
// keeping observation points on per-frame/per-callback success paths
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
