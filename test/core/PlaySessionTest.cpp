//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "perf/PlaySession.h"

#include "PerfTypes.h"
#include "pipeline/PipelineConfig.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace slark::perf;

// perfMonitorReportSession 的空实现，避免链接到 PerfMonitor 单例
namespace slark::perf {
void perfMonitorReportSession(PlaySession& /*session*/) {}
} // namespace slark::perf

// MARK: - 构造与基本属性

TEST(PlaySessionTest, ConstructorSetsSessionIdAndUrl) {
    PlaySession session("sess-001", "https://example.com/video.mp4");
    EXPECT_EQ(session.sessionId(), "sess-001");
    EXPECT_EQ(session.url(), "https://example.com/video.mp4");
    EXPECT_FALSE(session.isFinished());
    EXPECT_FALSE(session.isSuccess());
}

TEST(PlaySessionTest, ElapsedMsIsPositiveAfterConstruction) {
    PlaySession session("sess-002", "file://local.mp4");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_GT(session.elapsedMs(), 0.0);
}

TEST(PlaySessionTest, FirstFrameMsIsNegativeBeforeMarkFirstFrame) {
    PlaySession session("sess-003", "file://local.mp4");
    EXPECT_DOUBLE_EQ(session.firstFrameMs(), -1.0);
}

// MARK: - markFirstFrame 幂等性

TEST(PlaySessionTest, MarkFirstFrameRecordsPositiveMs) {
    PlaySession session("sess-004", "file://local.mp4");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    session.markFirstFrame();
    EXPECT_GT(session.firstFrameMs(), 0.0);
}

TEST(PlaySessionTest, MarkFirstFrameIsIdempotent) {
    PlaySession session("sess-005", "file://local.mp4");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    session.markFirstFrame();
    double first = session.firstFrameMs();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    session.markFirstFrame(); // 第二次调用不应更新
    EXPECT_DOUBLE_EQ(session.firstFrameMs(), first);
}

TEST(PlaySessionTest, MarkFirstFrameConcurrentOnlyOneWins) {
    PlaySession session("sess-006", "file://local.mp4");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    constexpr int32_t kThreadCount = 8;
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    for (int32_t i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&] {
            session.markFirstFrame();
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    // 无论多少线程调用，firstFrameMs 应该是一个合理的正值
    EXPECT_GT(session.firstFrameMs(), 0.0);
}

// MARK: - finish 幂等性

TEST(PlaySessionTest, FinishSetsIsFinishedAndSuccess) {
    PlaySession session("sess-007", "file://local.mp4");
    session.finish(true);
    EXPECT_TRUE(session.isFinished());
    EXPECT_TRUE(session.isSuccess());
}

TEST(PlaySessionTest, FinishIsIdempotent) {
    PlaySession session("sess-008", "file://local.mp4");
    session.finish(true);
    session.finish(false); // 第二次调用不应改变 success
    EXPECT_TRUE(session.isSuccess());
    EXPECT_TRUE(session.isFinished());
}

TEST(PlaySessionTest, FinishWithFailure) {
    PlaySession session("sess-009", "file://local.mp4");
    session.finish(false);
    EXPECT_TRUE(session.isFinished());
    EXPECT_FALSE(session.isSuccess());
}

TEST(PlaySessionTest, ElapsedMsStopsAfterFinish) {
    PlaySession session("sess-010", "file://local.mp4");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    session.finish(true);
    double elapsed1 = session.elapsedMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    double elapsed2 = session.elapsedMs();
    // finish 后 elapsed 应固定（endUs_ 已记录）
    EXPECT_DOUBLE_EQ(elapsed1, elapsed2);
}

// MARK: - record 阶段统计

TEST(PlaySessionTest, RecordAccumulatesStats) {
    PlaySession session("sess-011", "file://local.mp4");
    session.record("decode", 1000);
    session.record("decode", 2000);
    session.record("decode", 3000);

    auto stats = session.stageStatsSnapshot();
    ASSERT_NE(stats.find("decode"), stats.end());
    const auto& s = stats.at("decode");
    EXPECT_EQ(s.count, 3u);
    EXPECT_EQ(s.totalUs, 6000);
    EXPECT_EQ(s.minUs, 1000);
    EXPECT_EQ(s.maxUs, 3000);
    EXPECT_DOUBLE_EQ(s.avgMs(), 2.0);
}

TEST(PlaySessionTest, RecordMultipleStages) {
    PlaySession session("sess-012", "file://local.mp4");
    session.record("demux", 500);
    session.record("decode", 1000);
    session.record("render", 800);

    auto stats = session.stageStatsSnapshot();
    EXPECT_EQ(stats.size(), 3u);
    EXPECT_NE(stats.find("demux"), stats.end());
    EXPECT_NE(stats.find("decode"), stats.end());
    EXPECT_NE(stats.find("render"), stats.end());
}

TEST(PlaySessionTest, RecordDropsWhenExceedsMaxStageCount) {
    PlaySession session("sess-013", "file://local.mp4");
    // 填满 kMaxStageCount 个阶段
    for (size_t i = 0; i < PlaySession::kMaxStageCount; ++i) {
        session.record("stage_" + std::to_string(i), 100);
    }
    // 超出上限的阶段应被丢弃
    session.record("overflow_stage", 100);
    auto stats = session.stageStatsSnapshot();
    EXPECT_EQ(stats.size(), PlaySession::kMaxStageCount);
    EXPECT_EQ(stats.find("overflow_stage"), stats.end());
}

// MARK: - recordFrame 帧统计

TEST(PlaySessionTest, RecordFrameAccumulatesStats) {
    PlaySession session("sess-014", "file://local.mp4");
    session.recordFrame(16667); // ~60fps
    session.recordFrame(16667);
    session.recordFrame(16667);

    auto stats = session.frameStatsSnapshot();
    EXPECT_EQ(stats.totalFrames.load(), 3u);
    EXPECT_EQ(stats.totalDurationUs.load(), 50001);
    EXPECT_NEAR(stats.avgFps(), 60.0, 1.0);
}

TEST(PlaySessionTest, RecordFrameZeroDurationReturnsZeroFps) {
    PlaySession session("sess-015", "file://local.mp4");
    auto stats = session.frameStatsSnapshot();
    EXPECT_DOUBLE_EQ(stats.avgFps(), 0.0);
}

// MARK: - markMilestone

TEST(PlaySessionTest, MarkMilestoneRecordsLabel) {
    PlaySession session("sess-016", "file://local.mp4");
    session.markMilestone("open_start");
    session.markMilestone("first_packet");
    session.markMilestone("first_frame");

    auto milestones = session.milestonesSnapshot();
    ASSERT_EQ(milestones.size(), 3u);
    EXPECT_EQ(milestones[0].label, "open_start");
    EXPECT_EQ(milestones[1].label, "first_packet");
    EXPECT_EQ(milestones[2].label, "first_frame");
}

TEST(PlaySessionTest, MarkMilestoneTimestampIsMonotonic) {
    PlaySession session("sess-017", "file://local.mp4");
    session.markMilestone("a");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    session.markMilestone("b");

    auto milestones = session.milestonesSnapshot();
    ASSERT_EQ(milestones.size(), 2u);
    EXPECT_LE(milestones[0].timestampUs, milestones[1].timestampUs);
}

// MARK: - onResourceSample

TEST(PlaySessionTest, ResourceSampleAccumulatesStats) {
    PlaySession session("sess-018", "file://local.mp4");
    session.onResourceSample({.cpuPercent = 20.0, .rssKB = 100, .vmSizeKB = 200});
    session.onResourceSample({.cpuPercent = 40.0, .rssKB = 150, .vmSizeKB = 300});

    auto stats = session.resourceStatsSnapshot();
    EXPECT_EQ(stats.sampleCount, 2u);
    EXPECT_DOUBLE_EQ(stats.avgCpuPercent, 30.0);
    EXPECT_DOUBLE_EQ(stats.peakCpuPercent, 40.0);
    EXPECT_EQ(stats.peakRssKB, 150);
}

// MARK: - PerfStats 单元测试

TEST(PerfStatsTest, RecordUpdatesAllFields) {
    PerfStats stats;
    stats.record(100);
    stats.record(200);
    stats.record(50);

    EXPECT_EQ(stats.count, 3u);
    EXPECT_EQ(stats.totalUs, 350);
    EXPECT_EQ(stats.minUs, 50);
    EXPECT_EQ(stats.maxUs, 200);
    EXPECT_NEAR(stats.avgMs(), 350.0 / 3.0 / 1000.0, 1e-9);
}

TEST(PerfStatsTest, AvgMsIsZeroWhenNoRecords) {
    PerfStats stats;
    EXPECT_DOUBLE_EQ(stats.avgMs(), 0.0);
}

// MARK: - FrameStats 单元测试

TEST(FrameStatsTest, RecordAndDrop) {
    FrameStats stats;
    stats.record(16667);
    stats.record(16667);
    stats.recordDrop();

    EXPECT_EQ(stats.totalFrames.load(), 2u);
    EXPECT_EQ(stats.droppedFrames.load(), 1u);
    EXPECT_NEAR(stats.avgFps(), 60.0, 1.0);
}

// MARK: - ResourceStats 单元测试

TEST(ResourceStatsTest, RecordUpdatesAvgAndPeak) {
    ResourceStats stats;
    stats.record({.cpuPercent = 10.0, .rssKB = 50});
    stats.record({.cpuPercent = 30.0, .rssKB = 80});
    stats.record({.cpuPercent = 20.0, .rssKB = 60});

    EXPECT_EQ(stats.sampleCount, 3u);
    EXPECT_NEAR(stats.avgCpuPercent, 20.0, 1e-9);
    EXPECT_DOUBLE_EQ(stats.peakCpuPercent, 30.0);
    EXPECT_EQ(stats.peakRssKB, 80);
}

// MARK: - BackpressureConfig 验证

TEST(BackpressureConfigTest, ForLocalIsValid) {
    EXPECT_TRUE(slark::BackpressureConfig::forLocal().isValid());
}

TEST(BackpressureConfigTest, ForNetworkIsValid) {
    EXPECT_TRUE(slark::BackpressureConfig::forNetwork().isValid());
}

TEST(BackpressureConfigTest, ForLiveIsValid) {
    EXPECT_TRUE(slark::BackpressureConfig::forLive().isValid());
}

TEST(BackpressureConfigTest, InvalidConfigDetected) {
    auto cfg = slark::BackpressureConfig::forNetwork();
    cfg.demuxLowWatermarkDuration = cfg.demuxHighWatermarkDuration + 1.0; // 低水位 > 高水位
    EXPECT_FALSE(cfg.isValid());
}

TEST(BackpressureConfigTest, LiveConfigHasLowerWatermarks) {
    auto live = slark::BackpressureConfig::forLive();
    auto net = slark::BackpressureConfig::forNetwork();
    EXPECT_LT(live.demuxHighWatermarkDuration, net.demuxHighWatermarkDuration);
    EXPECT_LT(live.maxDropPerVsync, net.maxDropPerVsync);
}
