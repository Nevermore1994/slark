//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "pipeline/ClockSync.h"

#include "Time.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace slark;

// MARK: - setVideoFps / videoFps

TEST(ClockSyncExtTest, SetAndGetVideoFps) {
    ClockSync clock;
    EXPECT_DOUBLE_EQ(clock.videoFps(), 0.0);

    clock.setVideoFps(30.0);
    EXPECT_NEAR(clock.videoFps(), 30.0, 0.01);

    clock.setVideoFps(60.0);
    EXPECT_NEAR(clock.videoFps(), 60.0, 0.01);
}

TEST(ClockSyncExtTest, SetVideoFpsIgnoresZeroOrNegative) {
    ClockSync clock;
    clock.setVideoFps(24.0);
    clock.setVideoFps(0.0);
    EXPECT_NEAR(clock.videoFps(), 24.0, 0.01);

    clock.setVideoFps(-1.0);
    EXPECT_NEAR(clock.videoFps(), 24.0, 0.01);
}

TEST(ClockSyncExtTest, ResetClearsVideoFps) {
    ClockSync clock;
    clock.setVideoFps(60.0);
    clock.reset();
    EXPECT_DOUBLE_EQ(clock.videoFps(), 0.0);
}

// MARK: - seekTo 契约：重定位到目标，保留 fps/倍速

// 契约：seekTo 必须保留 videoFps 与 playbackRate（seek 不改变媒体帧率与用户倍速）。
// 该测试守护 performKeyframeSeek 修复所依赖的不变量——若有人误将 seekTo 改成 reset 语义即失败。
TEST(ClockSyncExtTest, SeekToPreservesFpsAndPlaybackRate) {
    ClockSync clock;
    clock.setVideoFps(30.0);
    clock.setPlaybackRate(1.5);
    clock.setHasAudio(true);

    clock.seekTo(Time::TimePoint::fromSeconds(1.434));

    EXPECT_NEAR(clock.videoFps(), 30.0, 0.01);
    EXPECT_NEAR(clock.playbackRate(), 1.5, 0.001);
}

// 契约：seekTo 后 audioTime/videoTime 均等于目标位置（而非归零）。
// 直接对应 bug：关键帧 seek 到 1.434 后主时钟必须从 1.434 起，不能从 0 起。
TEST(ClockSyncExtTest, SeekToAlignsAudioAndVideoTimeToTarget) {
    ClockSync clock;
    clock.setHasAudio(true);

    clock.seekTo(Time::TimePoint::fromSeconds(1.434));

    EXPECT_NEAR(clock.audioTime().second(), 1.434, 1e-4);
    EXPECT_NEAR(clock.videoTime().second(), 1.434, 1e-4);
    // 时钟未 running，masterTime 应直接返回 audioTime（不做 wall-clock 外推）
    EXPECT_NEAR(clock.masterTime().second(), 1.434, 1e-4);
}

// 契约：seekTo 递增 epoch，使 seek 前遗留的音频时间回调失效（不会把时钟拉回旧值）。
TEST(ClockSyncExtTest, SeekToBumpsEpochInvalidatingStaleAudioCallback) {
    ClockSync clock;
    clock.setHasAudio(true);
    const uint64_t staleEpoch = clock.currentAudioEpoch();

    clock.seekTo(Time::TimePoint::fromSeconds(1.434));

    // 携带旧 epoch 的回调应被丢弃，时钟保持在目标位置
    clock.setAudioTime(Time::TimePoint::fromSeconds(0.0), staleEpoch);
    EXPECT_NEAR(clock.audioTime().second(), 1.434, 1e-4);

    // 携带新 epoch 的回调正常生效
    clock.setAudioTime(Time::TimePoint::fromSeconds(1.5), clock.currentAudioEpoch());
    EXPECT_NEAR(clock.audioTime().second(), 1.5, 1e-4);
}

// MARK: - recordDrop / metrics

TEST(ClockSyncExtTest, RecordDropAccumulatesMetrics) {
    ClockSync clock;
    clock.recordDrop(3);
    clock.recordDrop(2);

    auto m = clock.metrics();
    EXPECT_EQ(m.dropCount, 5);
    EXPECT_EQ(m.chaseRoundCount, 2);
}

TEST(ClockSyncExtTest, ResetClearsDropMetrics) {
    ClockSync clock;
    clock.recordDrop(5);
    clock.reset();

    auto m = clock.metrics();
    EXPECT_EQ(m.dropCount, 0);
    EXPECT_EQ(m.chaseRoundCount, 0);
}

// MARK: - recordVsyncDuration / metrics

TEST(ClockSyncExtTest, RecordVsyncDurationStoresLatestValue) {
    ClockSync clock;
    clock.recordVsyncDuration(16667);
    EXPECT_EQ(clock.metrics().vsyncDurationUs, 16667);

    clock.recordVsyncDuration(33333);
    EXPECT_EQ(clock.metrics().vsyncDurationUs, 33333);
}

TEST(ClockSyncExtTest, ResetClearsVsyncDuration) {
    ClockSync clock;
    clock.recordVsyncDuration(16667);
    clock.reset();
    EXPECT_EQ(clock.metrics().vsyncDurationUs, 0);
}

// MARK: - evaluateAVSync 早到容忍窗口

TEST(ClockSyncExtTest, EvaluateAVSyncEarlyTolerancePreventsFalseHold) {
    // diff = -0.12（视频超前 120ms），earlyTolerance = 0.03
    // 实际超前 = 0.12 - 0.03 = 0.09 < kMinThreshold(0.04) → 应 Render
    // 注意：kMinThreshold = 0.04，scaledMinThreshold = 0.04
    // 条件：diffSec < -(scaledMinThreshold + earlyToleranceSec)
    //       -0.12 < -(0.04 + 0.03) = -0.07 → true → 进入 Hold 分支
    //       但 -diffSec = 0.12 < kMaxThreshold(10.0) → Hold
    // 所以 earlyTolerance 只能在 diff 接近阈值时起作用
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.06, 1.0, 0.03), AVSyncAction::Render);
    // diff = -0.06，earlyTolerance = 0.03
    // -(0.04 + 0.03) = -0.07，-0.06 > -0.07 → 不进入 Hold → Render
}

TEST(ClockSyncExtTest, EvaluateAVSyncEarlyToleranceStillHoldsWhenFarAhead) {
    // diff = -0.15，earlyTolerance = 0.03
    // -(0.04 + 0.03) = -0.07，-0.15 < -0.07 → Hold
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.15, 1.0, 0.03), AVSyncAction::Hold);
}

TEST(ClockSyncExtTest, EvaluateAVSyncZeroEarlyTolerance) {
    // earlyTolerance = 0 时行为与默认相同
    // diff = -0.03，在 kMinThreshold(0.04) 内 → Render
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.03, 1.0, 0.0), AVSyncAction::Render);
    // diff = -0.15，超过阈值 → Hold
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.15, 1.0, 0.0), AVSyncAction::Hold);
}

// MARK: - 并发安全：多线程同时写 audioTime/videoTime

TEST(ClockSyncExtTest, ConcurrentSetAudioVideoTimeIsThreadSafe) {
    ClockSync clock;
    constexpr int32_t kThreads = 8;
    constexpr int32_t kIter = 1000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads * 2);

    for (int32_t i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i] {
            for (int32_t j = 0; j < kIter; ++j) {
                clock.setAudioTime(
                    Time::TimePoint::fromSeconds(static_cast<double>(i * kIter + j) / 1000.0));
            }
        });
        threads.emplace_back([&, i] {
            for (int32_t j = 0; j < kIter; ++j) {
                clock.setVideoTime(
                    Time::TimePoint::fromSeconds(static_cast<double>(i * kIter + j) / 1000.0));
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    // 不崩溃即通过，值合法（非 NaN）
    EXPECT_GE(clock.audioTime().point(), 0u);
    EXPECT_GE(clock.videoTime().point(), 0u);
}

// MARK: - 并发安全：多线程同时 recordDrop

TEST(ClockSyncExtTest, ConcurrentRecordDropIsThreadSafe) {
    ClockSync clock;
    constexpr int32_t kThreads = 8;
    constexpr int32_t kDropPerThread = 100;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int32_t i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            for (int32_t j = 0; j < kDropPerThread; ++j) {
                clock.recordDrop(1);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(clock.metrics().dropCount, kThreads * kDropPerThread);
    EXPECT_EQ(clock.metrics().chaseRoundCount, kThreads * kDropPerThread);
}

// MARK: - evaluateAVSync 变速 + 早到容忍组合

TEST(ClockSyncExtTest, EvaluateAVSyncHighRateWithEarlyTolerance) {
    // 2x 速率，kMinThreshold * 2.0 = 0.08
    // diff = -0.10，earlyTolerance = 0.04
    // -(0.08 + 0.04) = -0.12，-0.10 > -0.12 → Render
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.10, 2.0, 0.04), AVSyncAction::Render);

    // diff = -0.15，earlyTolerance = 0.04
    // -(0.08 + 0.04) = -0.12，-0.15 < -0.12 → Hold
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.15, 2.0, 0.04), AVSyncAction::Hold);
}

// MARK: - kEarlyTolerance 常量

TEST(ClockSyncExtTest, EarlyToleranceConstantIs2ms) {
    EXPECT_DOUBLE_EQ(ClockSync::kEarlyTolerance, 0.002);
}
