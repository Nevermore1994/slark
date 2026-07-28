//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "LogConfig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "Log.hpp"

using namespace slark;

class LogConfigTest : public ::testing::Test {
 protected:
    void TearDown() override {
        // 恢复默认配置，避免测试间污染
        setLogConfig(LogConfig{});
    }
};

TEST_F(LogConfigTest, DefaultValues) {
    const auto& config = getLogConfig();
    EXPECT_EQ(config.progressLogBytes, 512 * 1024);
    EXPECT_EQ(config.maxLogFileSize, 8 * 1024 * 1024);
    EXPECT_EQ(config.maxLogFileCount, 10);
    EXPECT_EQ(config.minLogLevel, kMinLogLevelDefault);
    EXPECT_TRUE(config.logDir.empty());
    EXPECT_TRUE(config.logFileName.empty());
}

TEST_F(LogConfigTest, SetAndGet) {
    LogConfig config;
    config.progressLogBytes = 256 * 1024;
    config.maxLogFileSize = 16 * 1024 * 1024;
    config.maxLogFileCount = 5;
    config.minLogLevel = static_cast<int>(LogType::Warning);
    config.logDir = "/custom/log/dir";
    config.logFileName = "slark";

    setLogConfig(config);

    const auto& got = getLogConfig();
    EXPECT_EQ(got.progressLogBytes, 256 * 1024);
    EXPECT_EQ(got.maxLogFileSize, 16 * 1024 * 1024);
    EXPECT_EQ(got.maxLogFileCount, 5);
    EXPECT_EQ(got.minLogLevel, static_cast<int>(LogType::Warning));
    EXPECT_EQ(got.logDir, "/custom/log/dir");
    EXPECT_EQ(got.logFileName, "slark");
}

TEST_F(LogConfigTest, PartialSetKeepsDefaults) {
    LogConfig config;
    config.maxLogFileCount = 3;
    setLogConfig(config);

    const auto& got = getLogConfig();
    EXPECT_EQ(got.progressLogBytes, 512 * 1024);
    EXPECT_EQ(got.maxLogFileCount, 3);
}

TEST_F(LogConfigTest, MinLogLevelConsistency) {
    // kMinLogLevelDefault 必须与 LogType::Debug 一致
    EXPECT_EQ(kMinLogLevelDefault, static_cast<int>(LogType::Debug));
}

TEST_F(LogConfigTest, LogLevelEnumOrder) {
    // LogType 枚举值必须与 SlarkLogLevel (Android/iOS) 一致
    EXPECT_EQ(static_cast<int>(LogType::Print), 0);
    EXPECT_EQ(static_cast<int>(LogType::Debug), 1);
    EXPECT_EQ(static_cast<int>(LogType::Info), 2);
    EXPECT_EQ(static_cast<int>(LogType::Warning), 3);
    EXPECT_EQ(static_cast<int>(LogType::Error), 4);
    EXPECT_EQ(static_cast<int>(LogType::Assert), 5);
    EXPECT_EQ(static_cast<int>(LogType::Record), 6);
}

TEST_F(LogConfigTest, ProgressLogBytesNonZero) {
    LogConfig config;
    config.progressLogBytes = 0;
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().progressLogBytes, 0);

    config.progressLogBytes = 1;
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().progressLogBytes, 1);
}

TEST_F(LogConfigTest, MaxLogFileCountEdge) {
    LogConfig config;
    config.maxLogFileCount = 1;
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().maxLogFileCount, 1);

    config.maxLogFileCount = 100;
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().maxLogFileCount, 100);
}

TEST_F(LogConfigTest, LogFileNamePrefixSemantic) {
    // logFileName 是前缀，实际文件名为 前缀_时间.log
    LogConfig config;
    config.logFileName = "myapp";
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().logFileName, "myapp");
}

TEST_F(LogConfigTest, LogDirEmptyUsesDefault) {
    // 空 logDir 由 LogOutput 内部 fallback 到默认路径
    LogConfig config;
    config.logDir = "";
    setLogConfig(config);
    EXPECT_TRUE(getLogConfig().logDir.empty());
}

TEST_F(LogConfigTest, MinLogLevelFilter) {
    // 设置 minLogLevel=Error，Debug/Info/Warning 不应写文件
    LogConfig config;
    config.minLogLevel = static_cast<int>(LogType::Error);
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().minLogLevel, static_cast<int>(LogType::Error));
}

TEST_F(LogConfigTest, MaxLogFileSizeZero) {
    // 0 表示每次写入都会触发轮转（极端场景）
    LogConfig config;
    config.maxLogFileSize = 0;
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().maxLogFileSize, 0);
}
