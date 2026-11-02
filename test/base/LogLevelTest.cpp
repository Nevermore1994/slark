//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <atomic>
#include <format>
#include <string>

#include "Log.hpp"
#include "LogConfig.h"

namespace {
std::atomic<int> gProbeFormatCalls{0};
}  // namespace

struct LogLevelProbe {};

template <>
struct std::formatter<LogLevelProbe> : std::formatter<std::string_view> {
    auto format(const LogLevelProbe&, std::format_context& context) const {
        gProbeFormatCalls.fetch_add(1, std::memory_order_relaxed);
        return std::formatter<std::string_view>::format("probe", context);
    }
};

using namespace slark;

class LogLevelTest : public ::testing::Test {
protected:
    void SetUp() override {
        gProbeFormatCalls = 0;
    }

    void TearDown() override {
        setLogConfig(LogConfig{});
    }

    static void setMinLevel(int level) {
        LogConfig config;
        config.minLogLevel = level;
        setLogConfig(config);
    }

    static void setMinLevel(LogType level) {
        setMinLevel(static_cast<int>(level));
    }
};

TEST_F(LogLevelTest, LevelsBelowTheThresholdAreNeitherFormattedNorEvaluated) {
    setMinLevel(LogType::Warning);
    int evaluated = 0;

    LogD("{} {}", LogLevelProbe{}, ++evaluated);
    LogI("{} {}", LogLevelProbe{}, ++evaluated);
    EXPECT_EQ(gProbeFormatCalls.load(), 0);
    EXPECT_EQ(evaluated, 0);

    LogW("{} {}", LogLevelProbe{}, ++evaluated);
    EXPECT_EQ(gProbeFormatCalls.load(), 1);
    EXPECT_EQ(evaluated, 1);
}

TEST_F(LogLevelTest, ThrottledLogsSkipDisabledLevelsToo) {
    setMinLevel(LogType::Error);
    int evaluated = 0;

    LogIThrottle(0, "{}", ++evaluated);
    LogDThrottle(0, "{}", ++evaluated);
    EXPECT_EQ(evaluated, 0);
}

TEST_F(LogLevelTest, ConsoleSharesTheThresholdWithTheFile) {
    setMinLevel(LogType::Error);

    testing::internal::CaptureStdout();
    LogD("below-threshold-debug");
    LogW("below-threshold-warning");
    LogE("at-threshold-error");
    LogP("print-ignores-threshold");
    const auto console = testing::internal::GetCapturedStdout();

    EXPECT_EQ(console.find("below-threshold"), std::string::npos);
    EXPECT_NE(console.find("at-threshold-error"), std::string::npos);
    EXPECT_NE(console.find("print-ignores-threshold"), std::string::npos);
}

TEST_F(LogLevelTest, RecordIsFileOnly) {
    testing::internal::CaptureStdout();
    LogR("record-marker {}", LogLevelProbe{});
    const auto console = testing::internal::GetCapturedStdout();

    EXPECT_EQ(console.find("record-marker"), std::string::npos);
    EXPECT_EQ(gProbeFormatCalls.load(), 1);
}

TEST_F(LogLevelTest, ThresholdAppliesToEveryLevelExceptPrint) {
    constexpr LogType kFiltered[] = {LogType::Debug,   LogType::Info,   LogType::Warning,
                                     LogType::Error,   LogType::Assert, LogType::Record};
    for (int threshold = 0; threshold <= static_cast<int>(LogType::Record) + 1; ++threshold) {
        setMinLevel(threshold);
        EXPECT_TRUE(isLogEnabled(LogType::Print)) << "threshold " << threshold;
        for (const auto level : kFiltered) {
            EXPECT_EQ(isLogEnabled(level), static_cast<int>(level) >= threshold)
                << "level " << static_cast<int>(level) << " threshold " << threshold;
        }
    }
}
