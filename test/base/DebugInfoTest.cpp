//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DebugInfo.h"

#include <gtest/gtest.h>

#include <string>

#include "DebugLines.h"
#include "ProcessMetrics.h"
#include "TimerManager.h"

TEST(DebugInfoTest, FormatsAllTenGroupsInOrder) {
    slark::DebugInfo info;
    info.source = "URL: sample.mp4\n";
    info.network = "Host: example.com\n";
    const auto text = slark::formatDebugInfo(info);

    const std::string groups[] = {"[Source]", "[Video]",   "[Audio]",  "[Playback]", "[Sync]",
                                  "[Buffer]", "[Queue]",   "[Render]", "[Network]", "[System]"};
    size_t previous = 0;
    for (const auto& group : groups) {
        const auto position = text.find(group, previous);
        ASSERT_NE(position, std::string::npos) << group;
        previous = position + group.size();
    }
    EXPECT_NE(text.find("URL: sample.mp4"), std::string::npos);
    EXPECT_NE(text.find("Host: example.com"), std::string::npos);
}

TEST(DebugInfoTest, EmptyGroupsAreMarkedUnavailable) {
    const auto text = slark::formatDebugInfo(slark::DebugInfo{});
    size_t count = 0;
    size_t position = 0;
    while ((position = text.find("  n/a\n", position)) != std::string::npos) {
        ++count;
        position += 6;
    }
    EXPECT_EQ(count, 10u);
}

TEST(DebugInfoTest, AppendConcatenatesEachGroup) {
    slark::DebugInfo first;
    slark::DebugInfo second;
    first.queue = "Video decode queue: 1 frames\n";
    second.queue = "Audio decode queue: 2 frames\n";
    second.system = "CPU: 1 %\n";
    first.append(second);
    EXPECT_EQ(first.queue, "Video decode queue: 1 frames\nAudio decode queue: 2 frames\n");
    EXPECT_EQ(first.system, "CPU: 1 %\n");
    EXPECT_TRUE(first.video.empty());
}

TEST(DebugLinesTest, AddsValuesWithUnits) {
    slark::DebugLines lines;
    lines.add("Position", 12.5, "s");
    lines.add("Muted", false);
    lines.add("Codec", "H.264");
    EXPECT_EQ(lines.take(), "Position: 12.5 s\nMuted: false\nCodec: H.264\n");
}

TEST(ProcessMetricsTest, ReportsNonNegativeFootprintAndCpu) {
    int64_t rssKB = -1;
    int64_t vmSizeKB = -1;
    slark::perf::readProcessMemoryKB(rssKB, vmSizeKB);
    EXPECT_GE(rssKB, 0);
    EXPECT_GE(vmSizeKB, 0);
    const auto first = slark::perf::readProcessCpuPercent();
    const auto second = slark::perf::readProcessCpuPercent();
    EXPECT_GE(first, 0.0);
    EXPECT_GE(second, 0.0);
    EXPECT_LT(second, 10000.0);
}

TEST(ProcessMetricsTest, TimerAndConnectionCountsAreReadable) {
    const auto timers = slark::TimerManager::shareInstance().pendingTimerCount();
    EXPECT_LT(timers, 1000000u);
}
