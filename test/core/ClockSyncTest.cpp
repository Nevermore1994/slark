//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "pipeline/ClockSync.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "Time.hpp"

using namespace slark;


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


TEST(ClockSyncExtTest, SeekToPreservesFpsAndPlaybackRate) {
    ClockSync clock;
    clock.setVideoFps(30.0);
    clock.setPlaybackRate(1.5);
    clock.setHasAudio(true);

    clock.seekTo(Time::TimePoint::fromSeconds(1.434));

    EXPECT_NEAR(clock.videoFps(), 30.0, 0.01);
    EXPECT_NEAR(clock.playbackRate(), 1.5, 0.001);
}

TEST(ClockSyncExtTest, SeekToAlignsAudioAndVideoTimeToTarget) {
    ClockSync clock;
    clock.setHasAudio(true);

    clock.seekTo(Time::TimePoint::fromSeconds(1.434));

    EXPECT_NEAR(clock.audioTime().second(), 1.434, 1e-4);
    EXPECT_NEAR(clock.videoTime().second(), 1.434, 1e-4);
    EXPECT_NEAR(clock.masterTime().second(), 1.434, 1e-4);
}

TEST(ClockSyncExtTest, SeekToBumpsEpochInvalidatingStaleAudioCallback) {
    ClockSync clock;
    clock.setHasAudio(true);
    const uint64_t staleEpoch = clock.currentAudioEpoch();

    clock.seekTo(Time::TimePoint::fromSeconds(1.434));

    clock.setAudioTime(Time::TimePoint::fromSeconds(0.0), staleEpoch);
    EXPECT_NEAR(clock.audioTime().second(), 1.434, 1e-4);

    clock.setAudioTime(Time::TimePoint::fromSeconds(1.5), clock.currentAudioEpoch());
    EXPECT_NEAR(clock.audioTime().second(), 1.5, 1e-4);
}


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


TEST(ClockSyncExtTest, EvaluateAVSyncEarlyTolerancePreventsFalseHold) {
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.06, 1.0, 0.03), AVSyncAction::Render);
}

TEST(ClockSyncExtTest, EvaluateAVSyncEarlyToleranceStillHoldsWhenFarAhead) {
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.15, 1.0, 0.03), AVSyncAction::Hold);
}

TEST(ClockSyncExtTest, EvaluateAVSyncZeroEarlyTolerance) {
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.03, 1.0, 0.0), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.15, 1.0, 0.0), AVSyncAction::Hold);
}


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
    EXPECT_GE(clock.audioTime().point(), 0u);
    EXPECT_GE(clock.videoTime().point(), 0u);
}


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


TEST(ClockSyncExtTest, EvaluateAVSyncHighRateWithEarlyTolerance) {
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.10, 2.0, 0.04), AVSyncAction::Render);

    EXPECT_EQ(ClockSync::evaluateAVSync(-0.15, 2.0, 0.04), AVSyncAction::Hold);
}


TEST(ClockSyncExtTest, EarlyToleranceConstantIs2ms) {
    EXPECT_DOUBLE_EQ(ClockSync::kEarlyTolerance, 0.002);
}
