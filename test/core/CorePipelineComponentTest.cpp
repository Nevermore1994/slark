//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AVFrame.hpp"
#include "PipelineConfig.h"
#include "PlayerStateMachine.h"
#include "Time.hpp"
#include "pipeline/ClockSync.h"

#include <chrono>
#include <gtest/gtest.h>
#include <memory>

using namespace slark;

// MARK: -BackpressureConfig 测试

TEST(BackpressureConfigTest, ForLocalDefaultsAreValid) {
    auto cfg = BackpressureConfig::forLocal();
    EXPECT_TRUE(cfg.isValid());
    EXPECT_FALSE(cfg.isLiveSource);
    EXPECT_EQ(cfg.demuxHighWatermarkDuration, 30.0);
    EXPECT_EQ(cfg.demuxLowWatermarkDuration, 5.0);
    EXPECT_EQ(cfg.startBufferDuration, 0.0);
}

TEST(BackpressureConfigTest, ForNetworkDefaultsAreValid) {
    auto cfg = BackpressureConfig::forNetwork();
    EXPECT_TRUE(cfg.isValid());
    EXPECT_FALSE(cfg.isLiveSource);
    EXPECT_EQ(cfg.demuxHighWatermarkDuration, 10.0);
    EXPECT_EQ(cfg.demuxLowWatermarkDuration, 3.0);
    EXPECT_EQ(cfg.startBufferDuration, 2.0);
    EXPECT_EQ(cfg.rebufferScale, 1.5);
}

TEST(BackpressureConfigTest, ForLiveDefaultsAreValid) {
    auto cfg = BackpressureConfig::forLive();
    EXPECT_TRUE(cfg.isValid());
    EXPECT_TRUE(cfg.isLiveSource);
    EXPECT_EQ(cfg.demuxHighWatermarkDuration, 5.0);
    EXPECT_EQ(cfg.demuxLowWatermarkDuration, 1.0);
    EXPECT_EQ(cfg.startBufferDuration, 1.0);
    EXPECT_EQ(cfg.rebufferScale, 1.5);
    EXPECT_EQ(cfg.maxDropPerVsync, 4);
}

TEST(BackpressureConfigTest, InvalidWhenLowWatermarkExceedsHigh) {
    auto cfg = BackpressureConfig::forLocal();
    cfg.demuxLowWatermarkDuration = cfg.demuxHighWatermarkDuration + 1.0;
    EXPECT_FALSE(cfg.isValid());
}

TEST(BackpressureConfigTest, InvalidWhenStartBufferExceedsHighWatermark) {
    auto cfg = BackpressureConfig::forLocal();
    cfg.startBufferDuration = cfg.demuxHighWatermarkDuration + 1.0;
    EXPECT_FALSE(cfg.isValid());
}

TEST(BackpressureConfigTest, InvalidWhenStartBufferEqualsHighWatermark) {
    auto cfg = BackpressureConfig::forLocal();
    cfg.startBufferDuration = cfg.demuxHighWatermarkDuration;
    EXPECT_FALSE(cfg.isValid());
}

TEST(BackpressureConfigTest, ValidBoundaryLowEqualsZero) {
    auto cfg = BackpressureConfig::forLocal();
    cfg.demuxLowWatermarkDuration = 0.0;
    cfg.demuxHighWatermarkDuration = 1.0;
    cfg.startBufferDuration = 0.5;
    EXPECT_TRUE(cfg.isValid());
}

TEST(BackpressureConfigTest, ValidBoundaryLowJustBelowHigh) {
    auto cfg = BackpressureConfig{};
    cfg.demuxLowWatermarkDuration = 0.99;
    cfg.demuxHighWatermarkDuration = 1.0;
    cfg.startBufferDuration = 0.5;
    EXPECT_TRUE(cfg.isValid());
}

TEST(BackpressureConfigTest, ValidBoundaryHighAboveHighWatermark) {
    auto cfg = BackpressureConfig{};
    cfg.demuxLowWatermarkDuration = 0.5;
    cfg.demuxHighWatermarkDuration = 2.0;
    cfg.startBufferDuration = 1.0;
    EXPECT_TRUE(cfg.isValid());
}

TEST(BackpressureConfigTest, ValidBoundaryStartBufferJustBelowHigh) {
    auto cfg = BackpressureConfig{};
    cfg.demuxLowWatermarkDuration = 0.5;
    cfg.demuxHighWatermarkDuration = 2.0;
    cfg.startBufferDuration = 1.99;
    EXPECT_TRUE(cfg.isValid());
}

// MARK : -ClockSync 测试

TEST(ClockSyncTest, EvaluateAVSyncRenderWhenDiffWithinThreshold) {
    // kMinThreshold = 0.04 内 → Render
    EXPECT_EQ(ClockSync::evaluateAVSync(0.0), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(0.039), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.039), AVSyncAction::Render);
}

TEST(ClockSyncTest, EvaluateAVSyncRenderWhenDiffExceedsMaxThreshold) {
    // 负 diff 达到 kMaxThreshold(10.0) 以上，视为时钟异常 → Render
    EXPECT_EQ(ClockSync::evaluateAVSync(-10.0), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-100.0), AVSyncAction::Render);
}

TEST(ClockSyncTest, EvaluateAVSyncDropWhenVideoLagsAudio) {
    // 正 diff（视频落后音频）超过阈值但未异常 → Drop（追帧）
    EXPECT_EQ(ClockSync::evaluateAVSync(0.2), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(1.0), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(5.0), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(9.99), AVSyncAction::Drop);
}

TEST(ClockSyncTest, EvaluateAVSyncHoldWhenVideoLeadsAudio) {
    //     负 diff（视频超前音频）超过阈值但未异常 → Hold
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.2), AVSyncAction::Hold);
    EXPECT_EQ(ClockSync::evaluateAVSync(-1.0), AVSyncAction::Hold);
    EXPECT_EQ(ClockSync::evaluateAVSync(-9.99), AVSyncAction::Hold);
}

TEST(ClockSyncTest, EvaluateAVSyncThresholdBoundary) {
    //     kMinThreshold = 0.04 的精确边界测试
    EXPECT_EQ(ClockSync::evaluateAVSync(0.039), AVSyncAction::Render);
    //     刚好在 kMinThreshold(0.04) 内 → Render
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.039), AVSyncAction::Render);
    //     刚好在 kMinThreshold(0.04) 内 → Render（早到容忍窗口内）
    EXPECT_EQ(ClockSync::evaluateAVSync(0.041), AVSyncAction::Drop);
    //     刚好超过 kMinThreshold(0.04) → Drop
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.041), AVSyncAction::Hold);
    //     刚好超过 kMinThreshold(0.04) → Hold
}

TEST(ClockSyncTest, EvaluateAVSyncWithPlaybackRate) {
    //     变速播放时阈值随速率缩放
    //     实际 kMinThreshold = 0.04，2x 下 scaledMinThreshold = 0.08
    //     diff = 0.07，在 1x 下应 Drop(0.07>0.04)，在 2x 下应 Render(0.07<0.08)
    EXPECT_EQ(ClockSync::evaluateAVSync(0.07, 2.0), AVSyncAction::Render);

    //     diff = 0.25，在 1x 和 2x 下都应 Drop
    EXPECT_EQ(ClockSync::evaluateAVSync(0.25, 2.0), AVSyncAction::Drop);

    //     0.5x 速率下：kMinThreshold* 0.5 = 0.02
    //     diff = 0.08，在 0.5x 下应 Drop（0.08 > 0.02）
    EXPECT_EQ(ClockSync::evaluateAVSync(0.08, 0.5), AVSyncAction::Drop);

    //     diff = 0.01，在 0.5x 下应 Render（0.01 < 0.02）
    EXPECT_EQ(ClockSync::evaluateAVSync(0.01, 0.5), AVSyncAction::Render);
}

TEST(ClockSyncTest, SetAndGetPlaybackRate) {
    ClockSync clock;
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);

    clock.setPlaybackRate(2.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 2.0);

    clock.setPlaybackRate(0.5);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 0.5);
}

TEST(ClockSyncTest, PlaybackRateClampedToRange) {
    ClockSync clock;

    //     低于最小值应被忽略
    clock.setPlaybackRate(0.1);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);
    //     仍为默认值

    //     高于最大值应被忽略
    clock.setPlaybackRate(5.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);
}

TEST(ClockSyncTest, PlaybackRateBoundaryValues) {
    ClockSync clock;

    clock.setPlaybackRate(0.5);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 0.5);

    clock.setPlaybackRate(2.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 2.0);

    //     刚好等于边界值应被接受
    clock.setPlaybackRate(0.5);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 0.5);

    clock.setPlaybackRate(2.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 2.0);
}

TEST(ClockSyncTest, ResetClearsPlaybackRate) {
    ClockSync clock;
    clock.setPlaybackRate(1.5);
    clock.reset();
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);
}

TEST(ClockSyncTest, SetAndGetAudioVideoTime) {
    ClockSync clock;
    auto audioTp = Time::TimePoint::fromSeconds(1.5);
    auto videoTp = Time::TimePoint::fromSeconds(1.2);

    clock.setAudioTime(audioTp);
    clock.setVideoTime(videoTp);

    EXPECT_EQ(clock.audioTime(), audioTp);
    EXPECT_EQ(clock.videoTime(), videoTp);
}

TEST(ClockSyncTest, MasterTimePrefersAudioWhenHasAudio) {
    ClockSync clock;
    clock.setAudioTime(Time::TimePoint::fromSeconds(3.0));
    clock.setVideoTime(Time::TimePoint::fromSeconds(2.0));

    clock.setHasAudio(true);
    EXPECT_EQ(clock.masterTime(), Time::TimePoint::fromSeconds(3.0));

    clock.setHasAudio(false);
    EXPECT_EQ(clock.masterTime(), Time::TimePoint::fromSeconds(2.0));
}

TEST(ClockSyncTest, ResetClearsTimes) {
    ClockSync clock;
    clock.setAudioTime(Time::TimePoint::fromSeconds(5.0));
    clock.setVideoTime(Time::TimePoint::fromSeconds(4.0));
    clock.reset();
    EXPECT_EQ(clock.audioTime(), Time::TimePoint(0));
    EXPECT_EQ(clock.videoTime(), Time::TimePoint(0));
}

// MARK : -PlayerStateMachine isValidTransition 补充测试

TEST(PlayerStateMachineCoreTest, AllValidTransitions) {
    //     完整的状态转换矩阵验证

    struct Transition {
        PlayerState from;
        PlayerState to;
        bool expected;
    };

    std::vector<Transition> validTransitions = {
        {PlayerState::NotInited, PlayerState::Initializing, true},
        {PlayerState::Initializing, PlayerState::Prepared, true},
        {PlayerState::Initializing, PlayerState::Stop, true},
        {PlayerState::Initializing, PlayerState::Error, true},
        {PlayerState::Prepared, PlayerState::Buffering, true},
        {PlayerState::Prepared, PlayerState::Stop, true},
        {PlayerState::Prepared, PlayerState::Error, true},
        {PlayerState::Buffering, PlayerState::Ready, true},
        {PlayerState::Buffering, PlayerState::Playing, true},
        {PlayerState::Buffering, PlayerState::Pause, true},
        {PlayerState::Buffering, PlayerState::Stop, true},
        {PlayerState::Buffering, PlayerState::Error, true},
        {PlayerState::Ready, PlayerState::Playing, true},
        {PlayerState::Ready, PlayerState::Buffering, true},
        {PlayerState::Ready, PlayerState::Stop, true},
        {PlayerState::Ready, PlayerState::Error, true},
        {PlayerState::Playing, PlayerState::Pause, true},
        {PlayerState::Playing, PlayerState::Buffering, true},
        {PlayerState::Playing, PlayerState::Completed, true},
        {PlayerState::Playing, PlayerState::Stop, true},
        {PlayerState::Playing, PlayerState::Error, true},
        {PlayerState::Pause, PlayerState::Playing, true},
        {PlayerState::Pause, PlayerState::Buffering, true},
        {PlayerState::Pause, PlayerState::Stop, true},
        {PlayerState::Pause, PlayerState::Error, true},
        {PlayerState::Completed, PlayerState::Stop, true},
        {PlayerState::Error, PlayerState::Stop, true},
    };

    for (const auto& t : validTransitions) {
        EXPECT_EQ(PlayerStateMachine::isValidTransition(t.from, t.to), t.expected)
            << "Transition " << static_cast<int>(t.from) << " → " << static_cast<int>(t.to);
    }
}

TEST(PlayerStateMachineCoreTest, InvalidTransitions) {
    std::vector<std::pair<PlayerState, PlayerState>> invalidTransitions = {
        {PlayerState::NotInited, PlayerState::Prepared},
        {PlayerState::NotInited, PlayerState::Playing},
        {PlayerState::NotInited, PlayerState::Stop},
        {PlayerState::Stop, PlayerState::Playing},
        {PlayerState::Stop, PlayerState::Initializing},
        {PlayerState::Error, PlayerState::Playing},
        {PlayerState::Completed, PlayerState::Playing},
        {PlayerState::Completed, PlayerState::Pause},
    };

    for (const auto& [from, to] : invalidTransitions) {
        EXPECT_FALSE(PlayerStateMachine::isValidTransition(from, to))
            << "Invalid transition " << static_cast<int>(from) << " → " << static_cast<int>(to)
            << " should return false";
    }
}

