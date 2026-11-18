//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "LogConfig.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "Log.hpp"

using namespace slark;

class LogConfigTest : public ::testing::Test {
 protected:
    void TearDown() override {
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
    EXPECT_EQ(kMinLogLevelDefault, static_cast<int>(LogType::Debug));
}

TEST_F(LogConfigTest, LogLevelEnumOrder) {
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
    LogConfig config;
    config.logFileName = "myapp";
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().logFileName, "myapp");
}

TEST_F(LogConfigTest, LogDirEmptyUsesDefault) {
    LogConfig config;
    config.logDir = "";
    setLogConfig(config);
    EXPECT_TRUE(getLogConfig().logDir.empty());
}

TEST_F(LogConfigTest, MinLogLevelFilter) {
    LogConfig config;
    config.minLogLevel = static_cast<int>(LogType::Error);
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().minLogLevel, static_cast<int>(LogType::Error));
}

TEST_F(LogConfigTest, MaxLogFileSizeZero) {
    LogConfig config;
    config.maxLogFileSize = 0;
    setLogConfig(config);
    EXPECT_EQ(getLogConfig().maxLogFileSize, 0);
}

TEST_F(LogConfigTest, ConcurrentSetAndGetPublishesWholeSnapshots) {
    LogConfig first;
    first.progressLogBytes = 0x1111111111111111ULL;
    first.maxLogFileSize = 0x1111111111111111ULL;
    first.maxLogFileCount = 11;
    first.minLogLevel = 1;
    first.logDir = std::string(256, 'A');
    first.logFileName = std::string(256, 'a');

    LogConfig second;
    second.progressLogBytes = 0x2222222222222222ULL;
    second.maxLogFileSize = 0x2222222222222222ULL;
    second.maxLogFileCount = 22;
    second.minLogLevel = 2;
    second.logDir = std::string(256, 'B');
    second.logFileName = std::string(256, 'b');

    const auto matches = [](const LogConfig& expected, const LogConfig& actual) {
        return actual.progressLogBytes == expected.progressLogBytes &&
               actual.maxLogFileSize == expected.maxLogFileSize &&
               actual.maxLogFileCount == expected.maxLogFileCount &&
               actual.minLogLevel == expected.minLogLevel && actual.logDir == expected.logDir &&
               actual.logFileName == expected.logFileName;
    };

    setLogConfig(first);
    std::atomic<bool> start{false};
    std::atomic<int> tornReads{0};
    std::vector<std::thread> threads;
    threads.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 200; ++i) {
            setLogConfig((i & 1) ? second : first);
        }
    });
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < 2000; ++i) {
                const auto& snapshot = getLogConfig();
                if (!matches(first, snapshot) && !matches(second, snapshot)) {
                    tornReads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(tornReads.load(), 0);
}

TEST_F(LogConfigTest, SnapshotReferenceSurvivesLaterUpdates) {
    LogConfig initial;
    initial.progressLogBytes = 1234;
    initial.logDir = "stable/dir";
    initial.logFileName = "stable";
    setLogConfig(initial);

    const LogConfig& held = getLogConfig();
    for (uint64_t i = 0; i < 8; ++i) {
        LogConfig next;
        next.progressLogBytes = 2000 + i;
        next.logDir = std::string(128, 'x');
        next.logFileName = std::string(128, 'y');
        setLogConfig(next);
        EXPECT_EQ(getLogConfig().progressLogBytes, 2000 + i);
    }
    EXPECT_EQ(held.progressLogBytes, 1234);
    EXPECT_EQ(held.logDir, "stable/dir");
    EXPECT_EQ(held.logFileName, "stable");
}

TEST_F(LogConfigTest, ConcurrentWritersLeaveMinLevelMirrorAligned) {
    std::vector<std::thread> writers;
    for (int i = 0; i < 4; ++i) {
        writers.emplace_back([i] {
            for (int round = 0; round < 50; ++round) {
                LogConfig config;
                config.minLogLevel = (i + round) % 6;
                setLogConfig(config);
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    EXPECT_EQ(internal::gMinLogLevel.load(std::memory_order_relaxed),
              getLogConfig().minLogLevel);
}
