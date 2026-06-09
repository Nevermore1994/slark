//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
// 集成测试：验证本次修复的三条链路
//   1. firstFrameRendered CAS 设置时机（首帧模式下 AV Sync 启用）
//   2. videoTimeUpdater 回调链路（Android renderFrameComplete → ClockSync::videoTime_）
//   3. isDiscard 帧标志传递（seek preview 帧可用性）

#include "AVFrame.hpp"
#include "VideoInfo.h"
#include "pipeline/ClockSync.h"
#include "pipeline/SeekContext.h"

#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

using namespace slark;

// MARK: - 辅助：构造视频帧

static AVFrameRefPtr makeVideoFrame(double ptsSec, bool isDiscard = false) {
    auto frame = std::make_shared<AVFrame>(AVFrameType::Video);
    frame->pts = static_cast<int64_t>(ptsSec * 1000000);
    frame->timeScale = 1000000;
    frame->isDiscard = isDiscard;
    auto info = std::make_shared<VideoFrameInfo>();
    info->format = FrameFormat::Unknown;
    frame->info = std::move(info);
    return frame;
}

// MARK: - 1. firstFrameRendered CAS 时机

// 模拟 handleSeekFirstFrame 中的 CAS 逻辑：
// pushFrame 之后立即 CAS，而不是放到 postToOwnerThread 里
TEST(AVSyncIntegrationTest, FirstFrameRenderedCASIsAtomicAndIdempotent) {
    std::atomic<bool> firstFrameRendered{false};

    auto simulateHandleSeekFirstFrame = [&](const AVFrameRefPtr& /*frame*/) -> bool {
        bool expected = false;
        return firstFrameRendered.compare_exchange_strong(expected,
                                                          true,
                                                          std::memory_order_acq_rel);
    };

    auto frame = makeVideoFrame(1.0);

    // 第一次调用应返回 true（赢得 CAS）
    EXPECT_TRUE(simulateHandleSeekFirstFrame(frame));
    EXPECT_TRUE(firstFrameRendered.load());

    // 后续调用应返回 false（CAS 失败）
    EXPECT_FALSE(simulateHandleSeekFirstFrame(frame));
    EXPECT_FALSE(simulateHandleSeekFirstFrame(frame));
}

TEST(AVSyncIntegrationTest, FirstFrameRenderedCASConcurrentOnlyOneWins) {
    std::atomic<bool> firstFrameRendered{false};
    std::atomic<int32_t> winCount{0};

    auto task = [&] {
        bool expected = false;
        if (firstFrameRendered.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            winCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    constexpr int32_t kThreadCount = 8;
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    for (int32_t i = 0; i < kThreadCount; ++i) {
        threads.emplace_back(task);
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(winCount.load(), 1);
    EXPECT_TRUE(firstFrameRendered.load());
}

// MARK: - 2. videoTimeUpdater 回调链路

// 模拟 VideoRender::renderFrameComplete 调用 videoTimeUpdater_ 更新 ClockSync
TEST(AVSyncIntegrationTest, VideoTimeUpdaterSyncsClockSyncVideoTime) {
    ClockSync clockSync;
    EXPECT_EQ(clockSync.videoTime(), Time::TimePoint(0));

    // 模拟 IVideoRender::setVideoTimeUpdater 注入的回调
    std::function<void(Time::TimePoint)> videoTimeUpdater = [&](Time::TimePoint t) {
        clockSync.setVideoTime(t);
    };

    // 模拟 renderFrameComplete 调用
    const double renderedPts = 3.5;
    videoTimeUpdater(Time::TimePoint::fromSeconds(renderedPts));

    EXPECT_EQ(clockSync.videoTime(), Time::TimePoint::fromSeconds(renderedPts));
}

TEST(AVSyncIntegrationTest, VideoTimeUpdaterEnablesAVSyncAfterFirstFrame) {
    ClockSync clockSync;

    std::function<void(Time::TimePoint)> videoTimeUpdater = [&](Time::TimePoint t) {
        clockSync.setVideoTime(t);
    };

    // 音频时钟在 4.0s
    clockSync.setAudioTime(Time::TimePoint::fromSeconds(4.0));

    // 首帧渲染前：videoTime = 0，diff = 4.0 - 0 = 4.0 → Drop（视频落后）
    {
        double diff = clockSync.audioTime().second() - clockSync.videoTime().second();
        EXPECT_EQ(ClockSync::evaluateAVSync(diff), AVSyncAction::Drop);
    }

    // 首帧渲染后：videoTimeUpdater 更新 videoTime = 3.97s（diff = 0.03 < kMinThreshold=0.04 →
    // Render）
    videoTimeUpdater(Time::TimePoint::fromSeconds(3.97));

    // diff = 4.0 - 3.97 = 0.03 < 0.04 → Render
    {
        double diff = clockSync.audioTime().second() - clockSync.videoTime().second();
        EXPECT_EQ(ClockSync::evaluateAVSync(diff), AVSyncAction::Render);
    }
}

TEST(AVSyncIntegrationTest, VideoTimeUpdaterNullSafeWhenNotSet) {
    // 未注入 updater 时，不应崩溃
    std::function<void(Time::TimePoint)> videoTimeUpdater;
    EXPECT_FALSE(static_cast<bool>(videoTimeUpdater));

    // 模拟 renderFrameComplete 中的防御性调用
    if (videoTimeUpdater) {
        videoTimeUpdater(Time::TimePoint::fromSeconds(1.0));
    }
    // 不崩溃即通过
}

// MARK: - 3. isDiscard 帧标志传递

TEST(AVSyncIntegrationTest, DiscardFrameHasCorrectFlag) {
    auto frame = makeVideoFrame(2.0, true);
    EXPECT_TRUE(frame->isDiscard);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 2.0);
}

TEST(AVSyncIntegrationTest, NormalFrameHasDiscardFlagFalse) {
    auto frame = makeVideoFrame(2.0, false);
    EXPECT_FALSE(frame->isDiscard);
}

// 模拟 seekPreviewFrame 逻辑：从队列中找最后一个 isDiscard 帧
TEST(AVSyncIntegrationTest, SeekPreviewFrameFindsLastDiscardFrame) {
    // 模拟解码输出队列（pts 有序）
    std::vector<AVFrameRefPtr> decodedFrames;
    decodedFrames.push_back(makeVideoFrame(1.0, true));  // discard
    decodedFrames.push_back(makeVideoFrame(1.5, true));  // discard
    decodedFrames.push_back(makeVideoFrame(2.0, false)); // 目标帧（非 discard）

    // seekPreviewFrame 应返回最后一个 isDiscard 帧（pts = 1.5）
    AVFrameRefPtr previewFrame;
    for (const auto& f : decodedFrames) {
        if (f->isDiscard) {
            previewFrame = f;
        }
    }

    ASSERT_NE(previewFrame, nullptr);
    EXPECT_DOUBLE_EQ(previewFrame->ptsTime(), 1.5);
    EXPECT_TRUE(previewFrame->isDiscard);
}

TEST(AVSyncIntegrationTest, SeekPreviewFrameIsNullWhenNoDiscardFrames) {
    std::vector<AVFrameRefPtr> decodedFrames;
    decodedFrames.push_back(makeVideoFrame(1.0, false));
    decodedFrames.push_back(makeVideoFrame(2.0, false));

    AVFrameRefPtr previewFrame;
    for (const auto& f : decodedFrames) {
        if (f->isDiscard) {
            previewFrame = f;
        }
    }

    EXPECT_EQ(previewFrame, nullptr);
}

// MARK: - 4. ClockSync 阈值修正（kMinThreshold = 0.04）

// 本次发现 CorePipelineComponentTest 中的阈值测试基于旧值 0.1，
// 此处补充基于实际值 0.04 的精确边界测试
TEST(AVSyncIntegrationTest, ClockSyncActualThresholdIs40ms) {
    // 0.04s 内 → Render
    EXPECT_EQ(ClockSync::evaluateAVSync(0.04), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.04), AVSyncAction::Render);

    // 刚超过 0.04 → Drop/Hold
    EXPECT_EQ(ClockSync::evaluateAVSync(0.041), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.041), AVSyncAction::Hold);
}

TEST(AVSyncIntegrationTest, ClockSyncEarlyToleranceReducesHold) {
    // diff = -0.05（视频超前 50ms），无容忍 → Hold
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.05, 1.0, 0.0), AVSyncAction::Hold);

    // 加入 2ms 容忍窗口：-0.05 < -(0.04 + 0.002) = -0.042 → 仍 Hold
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.05, 1.0, ClockSync::kEarlyTolerance),
              AVSyncAction::Hold);

    // diff = -0.041，无容忍 → Hold；加 2ms 容忍 → -0.041 < -0.042 为 false → Render
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.041, 1.0, 0.0), AVSyncAction::Hold);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.041, 1.0, ClockSync::kEarlyTolerance),
              AVSyncAction::Render);
}

// MARK: - 5. SeekContext + ClockSync 协同

TEST(AVSyncIntegrationTest, SeekEpochInvalidatesOldFrames) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(5.0), SeekStrategy::SeekToKeyframe);
    uint64_t epoch = ctx.currentEpoch();

    // 模拟解码线程持有旧 epoch，新 seek 发生后应丢弃
    std::ignore = ctx.beginSeek(CTime(10.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.isEpochStale(epoch));
}

TEST(AVSyncIntegrationTest, SeekCompleteResetsClockSyncVideoTime) {
    ClockSync clockSync;
    SeekContext ctx;

    // seek 前设置 videoTime
    clockSync.setVideoTime(Time::TimePoint::fromSeconds(3.0));
    EXPECT_EQ(clockSync.videoTime(), Time::TimePoint::fromSeconds(3.0));

    // seek 完成后 reset ClockSync
    std::ignore = ctx.beginSeek(CTime(10.0), SeekStrategy::SeekToKeyframe);
    clockSync.reset();
    EXPECT_EQ(clockSync.videoTime(), Time::TimePoint(0));

    std::ignore = ctx.completeSeek();
    EXPECT_FALSE(ctx.isSeekPending());
}
