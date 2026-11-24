//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "DebugMetrics.h"

namespace {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t begin = 0;
    while (begin < text.size()) {
        const size_t end = text.find('\n', begin);
        if (end == std::string::npos) {
            break;
        }
        lines.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

}  // namespace

TEST(DebugMetricsTest, FirstTickOnlyPrimesTheCpuBaseline) {
    auto& metrics = slark::perf::DebugMetrics::instance();
    metrics.reset();

    const auto first = metrics.tick();
    EXPECT_TRUE(first.empty());
    EXPECT_EQ(metrics.count(), 0u);

    const auto second = metrics.tick();
    EXPECT_EQ(splitLines(second).size(), 1u);
    EXPECT_EQ(metrics.count(), 1u);
}

TEST(DebugMetricsTest, TickReturnsOneCsvRowPerSample) {
    auto& metrics = slark::perf::DebugMetrics::instance();
    metrics.reset();

    metrics.tick();
    const auto two = metrics.tick();
    const auto three = metrics.tick();
    EXPECT_EQ(splitLines(two).size(), 1u);
    EXPECT_EQ(splitLines(three).size(), 2u);
    EXPECT_EQ(metrics.count(), 2u);

    const auto lines = splitLines(three);
    const std::string row = lines.back();
    EXPECT_EQ(std::count(row.begin(), row.end(), ','), 3);
}

TEST(DebugMetricsTest, HistoryKeepsOldestToNewestAndWrapsAtCapacity) {
    auto& metrics = slark::perf::DebugMetrics::instance();
    metrics.reset();

    for (size_t i = 0; i < slark::perf::kDebugHistoryCapacity + 20; ++i) {
        metrics.sample();
    }
    EXPECT_EQ(metrics.count(), slark::perf::kDebugHistoryCapacity);

    const auto lines = splitLines(metrics.tick());
    ASSERT_EQ(lines.size(), slark::perf::kDebugHistoryCapacity);
    const int64_t first = std::stoll(lines.front().substr(0, lines.front().find(',')));
    const int64_t last = std::stoll(lines.back().substr(0, lines.back().find(',')));
    EXPECT_LE(first, last);
}

TEST(DebugMetricsTest, ResetClearsHistoryAndRearmsTheBaseline) {
    auto& metrics = slark::perf::DebugMetrics::instance();
    metrics.reset();
    metrics.sample();
    metrics.sample();
    ASSERT_GT(metrics.count(), 0u);

    metrics.reset();
    EXPECT_EQ(metrics.count(), 0u);
    EXPECT_EQ(metrics.latest().timestampUs, 0);
    EXPECT_EQ(metrics.latestCpuPercent(), 0.0);

    // After reset the first sample primes the baseline again, so no fake 0 is recorded.
    metrics.sample();
    EXPECT_EQ(metrics.count(), 0u);
    metrics.sample();
    EXPECT_EQ(metrics.count(), 1u);
}

TEST(DebugMetricsTest, CpuSampleIsFiniteAndBoundedForTheWindow) {
    auto& metrics = slark::perf::DebugMetrics::instance();
    metrics.reset();

    metrics.sample();
    EXPECT_DOUBLE_EQ(metrics.latestCpuPercent(), 0.0);
    metrics.sample();
    const double cpu = metrics.latestCpuPercent();
    EXPECT_TRUE(std::isfinite(cpu));
    EXPECT_GE(cpu, 0.0);
}
