//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "AVFrame.hpp"
#include "FlowConfig.h"
#include "PlayerStateMachine.h"
#include "Time.hpp"
#include "pipeline/ClockSync.h"
#include "pipeline/FlowConfigDefaults.h"

using namespace slark;
using namespace slark::pipeline;

namespace {
constexpr MediaSourceType kLocal = MediaSourceType::Local;
constexpr MediaSourceType kNetwork = MediaSourceType::Network;
constexpr MediaSourceType kLive = MediaSourceType::Live;
}  // namespace

TEST(FlowConfigTest, SourceTypeIsDerivedFromStreamAndLiveStatus) {
    EXPECT_EQ(sourceTypeOf(StreamType::File, false), kLocal);
    EXPECT_EQ(sourceTypeOf(StreamType::Http, false), kNetwork);
    EXPECT_EQ(sourceTypeOf(StreamType::HLS, false), kNetwork);
    EXPECT_EQ(sourceTypeOf(StreamType::Http, true), kLive);
}

TEST(FlowConfigTest, NoOverrideUsesTheSourceDefault) {
    const auto resolved = FlowConfigDefaults::resolve(std::nullopt, kLocal);
    EXPECT_TRUE(resolved.isValid());
    EXPECT_DOUBLE_EQ(resolved.source.highWatermarkSec,
                     FlowConfigDefaults::forSource(kLocal).source.highWatermarkSec);
}

TEST(FlowConfigTest, CallerOverrideWinsOverDefault) {
    FlowConfig custom = FlowConfigDefaults::forSource(kLocal);
    custom.source.highWatermarkSec = 5.0;
    custom.source.lowWatermarkSec = 1.0;
    ASSERT_TRUE(custom.isValid());

    const auto resolved = FlowConfigDefaults::resolve(custom, kLocal);
    EXPECT_DOUBLE_EQ(resolved.source.highWatermarkSec, 5.0);
    EXPECT_DOUBLE_EQ(resolved.source.lowWatermarkSec, 1.0);
}

TEST(FlowConfigTest, ExplicitConfigReplacesSourceDefaultsAsAWhole) {
    const FlowConfig custom;
    ASSERT_TRUE(custom.isValid());

    const auto resolved = FlowConfigDefaults::resolve(custom, kLocal);
    EXPECT_DOUBLE_EQ(resolved.source.lowWatermarkSec, custom.source.lowWatermarkSec);
    EXPECT_DOUBLE_EQ(resolved.source.highWatermarkSec, custom.source.highWatermarkSec);
    EXPECT_DOUBLE_EQ(resolved.buffering.startBufferSec, custom.buffering.startBufferSec);
    EXPECT_NE(resolved.source.highWatermarkSec,
              FlowConfigDefaults::forSource(kLocal).source.highWatermarkSec);
}

TEST(FlowConfigTest, InvalidOverrideFallsBackToDefault) {
    FlowConfig bad = FlowConfigDefaults::forSource(kLocal);
    bad.source.lowWatermarkSec = bad.source.highWatermarkSec + 1.0;
    ASSERT_FALSE(bad.isValid());

    const auto resolved = FlowConfigDefaults::resolve(bad, kLocal);
    EXPECT_TRUE(resolved.isValid());
    EXPECT_DOUBLE_EQ(resolved.source.highWatermarkSec,
                     FlowConfigDefaults::forSource(kLocal).source.highWatermarkSec);
}

TEST(FlowConfigTest, ForLocalDefaultsAreValid) {
    auto cfg = FlowConfigDefaults::forSource(kLocal);
    EXPECT_TRUE(cfg.isValid());
    EXPECT_DOUBLE_EQ(cfg.source.highWatermarkSec, 15.0);
    EXPECT_DOUBLE_EQ(cfg.source.lowWatermarkSec, 2.0);
    EXPECT_DOUBLE_EQ(cfg.buffering.startBufferSec, 0.0);
}

TEST(FlowConfigTest, ForNetworkDefaultsAreValid) {
    auto cfg = FlowConfigDefaults::forSource(kNetwork);
    EXPECT_TRUE(cfg.isValid());
    EXPECT_DOUBLE_EQ(cfg.source.highWatermarkSec, 10.0);
    EXPECT_DOUBLE_EQ(cfg.source.lowWatermarkSec, 3.0);
    EXPECT_DOUBLE_EQ(cfg.buffering.startBufferSec, 2.0);
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferScale, 1.5);
    EXPECT_EQ(cfg.buffering.starvationThreshold, 5);
    EXPECT_EQ(cfg.audioDecoder.lowWatermarkFrames, 4);
    EXPECT_EQ(cfg.audioDecoder.highWatermarkFrames, 16);
    EXPECT_EQ(cfg.videoDecoder.lowWatermarkFrames, 2);
    EXPECT_EQ(cfg.videoDecoder.highWatermarkFrames, 8);
    EXPECT_EQ(cfg.catchUp.maxDropPerVsync, 8);
    EXPECT_EQ(cfg.catchUp.seekFastForwardMs, 300);
}

TEST(FlowConfigTest, ForLiveDefaultsAreValid) {
    auto cfg = FlowConfigDefaults::forSource(kLive);
    EXPECT_TRUE(cfg.isValid());
    EXPECT_DOUBLE_EQ(cfg.source.highWatermarkSec, 5.0);
    EXPECT_DOUBLE_EQ(cfg.source.lowWatermarkSec, 1.0);
    EXPECT_DOUBLE_EQ(cfg.buffering.startBufferSec, 1.0);
    EXPECT_EQ(cfg.audioDecoder.lowWatermarkFrames, 2);
    EXPECT_EQ(cfg.audioDecoder.highWatermarkFrames, 8);
    EXPECT_EQ(cfg.videoDecoder.lowWatermarkFrames, 1);
    EXPECT_EQ(cfg.videoDecoder.highWatermarkFrames, 4);
    EXPECT_EQ(cfg.catchUp.maxDropPerVsync, 4);
}

TEST(FlowConfigTest, LiveTrimsEverySliceComparedToNetwork) {
    auto live = FlowConfigDefaults::forSource(kLive);
    auto net = FlowConfigDefaults::forSource(kNetwork);
    EXPECT_LT(live.source.highWatermarkSec, net.source.highWatermarkSec);
    EXPECT_LT(live.videoDecoder.highWatermarkFrames, net.videoDecoder.highWatermarkFrames);
    EXPECT_LT(live.catchUp.maxDropPerVsync, net.catchUp.maxDropPerVsync);
}

TEST(FlowConfigTest, EverySourceTypeValidatesWholeConfig) {
    for (auto type : {kLocal, kNetwork, kLive}) {
        auto cfg = FlowConfigDefaults::forSource(type);
        EXPECT_TRUE(cfg.isValid());
        EXPECT_TRUE(cfg.source.isValid());
        EXPECT_TRUE(cfg.audioDecoder.isValid());
        EXPECT_TRUE(cfg.videoDecoder.isValid());
        EXPECT_TRUE(cfg.buffering.isValid());
        EXPECT_TRUE(cfg.catchUp.isValid());
    }
}

// A broken slice must invalidate the aggregate, otherwise a bad watermark reaches a
// module that never validates it.
TEST(FlowConfigTest, InvalidSliceMakesWholeConfigInvalid) {
    {
        auto cfg = FlowConfigDefaults::forSource(kNetwork);
        cfg.source.lowWatermarkSec = cfg.source.highWatermarkSec + 1.0;
        EXPECT_FALSE(cfg.source.isValid());
        EXPECT_FALSE(cfg.isValid());
    }
    {
        auto cfg = FlowConfigDefaults::forSource(kNetwork);
        cfg.audioDecoder.highWatermarkFrames = 0;
        EXPECT_FALSE(cfg.audioDecoder.isValid());
        EXPECT_FALSE(cfg.isValid());
    }
    {
        auto cfg = FlowConfigDefaults::forSource(kNetwork);
        cfg.videoDecoder.highWatermarkFrames = cfg.videoDecoder.lowWatermarkFrames;
        EXPECT_FALSE(cfg.videoDecoder.isValid());
        EXPECT_FALSE(cfg.isValid());
    }
    {
        auto cfg = FlowConfigDefaults::forSource(kNetwork);
        cfg.buffering.starvationThreshold = 0;
        EXPECT_FALSE(cfg.buffering.isValid());
        EXPECT_FALSE(cfg.isValid());
    }
    {
        auto cfg = FlowConfigDefaults::forSource(kNetwork);
        cfg.catchUp.maxDropPerVsync = 0;
        EXPECT_FALSE(cfg.catchUp.isValid());
        EXPECT_FALSE(cfg.isValid());
    }
}

// Cross-slice constraint: production stops at the high watermark, so a rebuffer target
// at or above it can never be reached and rebuffering would never exit.
TEST(FlowConfigTest, RebufferTargetMustStayBelowSourceHighWatermark) {
    auto cfg = FlowConfigDefaults::forSource(kNetwork);
    ASSERT_DOUBLE_EQ(cfg.buffering.rebufferTargetSec(), 3.0);
    ASSERT_LT(cfg.buffering.rebufferTargetSec(), cfg.source.highWatermarkSec);

    cfg.buffering.startBufferSec = cfg.source.highWatermarkSec / cfg.buffering.rebufferScale;
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferExitSec(), 15.0);
    EXPECT_FALSE(cfg.isValid());

    cfg.buffering.startBufferSec = 2.0;
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferTargetSec(), 3.0);
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferExitSec(), 4.5);
    EXPECT_TRUE(cfg.isValid());

    cfg.buffering.rebufferScale = 2.0;
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferTargetSec(), 4.0);
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferExitSec(), 8.0);
    EXPECT_TRUE(cfg.isValid());

    cfg.buffering.rebufferScale = 1.5;
    cfg.buffering.startBufferSec = 5.0;
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferTargetSec(), 7.5);
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferExitSec(), 11.25);
    EXPECT_FALSE(cfg.isValid()) << "exit level above the high watermark never recovers";
}

TEST(FlowConfigTest, SourceWatermarkBoundaries) {
    auto cfg = FlowConfigDefaults::forSource(kNetwork);
    cfg.source.lowWatermarkSec = 0.0;
    EXPECT_TRUE(cfg.source.isValid());

    cfg.source.lowWatermarkSec = cfg.source.highWatermarkSec;
    EXPECT_FALSE(cfg.source.isValid());

    cfg.source.lowWatermarkSec = -1.0;
    EXPECT_FALSE(cfg.source.isValid());
}

TEST(FlowConfigTest, DecoderWatermarkBoundaries) {
    DecoderBackpressureConfig cfg;
    cfg.lowWatermarkFrames = 0;
    EXPECT_TRUE(cfg.isValid());

    cfg.lowWatermarkFrames = cfg.highWatermarkFrames;
    EXPECT_FALSE(cfg.isValid());

    cfg.lowWatermarkFrames = -1;
    EXPECT_FALSE(cfg.isValid());
}

TEST(FlowConfigTest, BufferingConfigBoundaries) {
    BufferingConfig cfg;
    cfg.startBufferSec = 0.0;
    EXPECT_TRUE(cfg.isValid());

    cfg.rebufferScale = 1.0;
    EXPECT_TRUE(cfg.isValid());

    cfg.rebufferScale = 0.99;
    EXPECT_FALSE(cfg.isValid());

    cfg.rebufferScale = 1.5;
    cfg.starvationThreshold = 1;
    EXPECT_TRUE(cfg.isValid());

    cfg.starvationThreshold = 0;
    EXPECT_FALSE(cfg.isValid());

    cfg.starvationThreshold = -1;
    EXPECT_FALSE(cfg.isValid());
}

TEST(FlowConfigTest, CatchUpConfigBoundaries) {
    CatchUpConfig cfg;
    cfg.maxDropPerVsync = 1;
    cfg.seekFastForwardMs = 1;
    EXPECT_TRUE(cfg.isValid());

    cfg.maxDropPerVsync = 0;
    EXPECT_FALSE(cfg.isValid());

    cfg.maxDropPerVsync = -1;
    EXPECT_FALSE(cfg.isValid());

    cfg.maxDropPerVsync = 1;
    cfg.seekFastForwardMs = 0;
    EXPECT_FALSE(cfg.isValid());

    cfg.seekFastForwardMs = -1;
    EXPECT_FALSE(cfg.isValid());
}


TEST(ClockSyncTest, EvaluateAVSyncRenderWhenDiffWithinThreshold) {
    EXPECT_EQ(ClockSync::evaluateAVSync(0.0), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(0.039), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.039), AVSyncAction::Render);
}

TEST(ClockSyncTest, EvaluateAVSyncRenderWhenDiffExceedsMaxThreshold) {
    EXPECT_EQ(ClockSync::evaluateAVSync(-10.0), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-100.0), AVSyncAction::Render);
}

TEST(ClockSyncTest, EvaluateAVSyncDropWhenVideoLagsAudio) {
    EXPECT_EQ(ClockSync::evaluateAVSync(0.2), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(1.0), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(5.0), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(9.99), AVSyncAction::Drop);
}

TEST(ClockSyncTest, EvaluateAVSyncHoldWhenVideoLeadsAudio) {
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.2), AVSyncAction::Hold);
    EXPECT_EQ(ClockSync::evaluateAVSync(-1.0), AVSyncAction::Hold);
    EXPECT_EQ(ClockSync::evaluateAVSync(-9.99), AVSyncAction::Hold);
}

TEST(ClockSyncTest, EvaluateAVSyncThresholdBoundary) {
    EXPECT_EQ(ClockSync::evaluateAVSync(0.039), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.039), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(0.041), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.041), AVSyncAction::Hold);
}

TEST(ClockSyncTest, EvaluateAVSyncWithPlaybackRate) {
    EXPECT_EQ(ClockSync::evaluateAVSync(0.07, 2.0), AVSyncAction::Render);

    EXPECT_EQ(ClockSync::evaluateAVSync(0.25, 2.0), AVSyncAction::Drop);

    EXPECT_EQ(ClockSync::evaluateAVSync(0.08, 0.5), AVSyncAction::Drop);

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

    clock.setPlaybackRate(0.1);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);

    clock.setPlaybackRate(5.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);
}

TEST(ClockSyncTest, PlaybackRateBoundaryValues) {
    ClockSync clock;

    clock.setPlaybackRate(0.5);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 0.5);

    clock.setPlaybackRate(2.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 2.0);

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


TEST(PlayerStateMachineCoreTest, AllValidTransitions) {

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
        {PlayerState::Completed, PlayerState::Error},
    };

    for (const auto& [from, to] : invalidTransitions) {
        EXPECT_FALSE(PlayerStateMachine::isValidTransition(from, to))
            << "Invalid transition " << static_cast<int>(from) << " → " << static_cast<int>(to)
            << " should return false";
    }
}
