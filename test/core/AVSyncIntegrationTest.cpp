//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include "AVFrame.hpp"
#include "VideoInfo.h"
#include "pipeline/ClockSync.h"
#include "pipeline/SeekContext.h"

using namespace slark;


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


TEST(AVSyncIntegrationTest, FirstFrameRenderedCASIsAtomicAndIdempotent) {
    std::atomic<bool> firstFrameRendered{false};

    auto simulateHandleSeekFirstFrame = [&](const AVFrameRefPtr& ) -> bool {
        bool expected = false;
        return firstFrameRendered.compare_exchange_strong(expected, true,
                                                          std::memory_order_acq_rel);
    };

    auto frame = makeVideoFrame(1.0);

    EXPECT_TRUE(simulateHandleSeekFirstFrame(frame));
    EXPECT_TRUE(firstFrameRendered.load());

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


TEST(AVSyncIntegrationTest, VideoTimeUpdaterSyncsClockSyncVideoTime) {
    ClockSync clockSync;
    EXPECT_EQ(clockSync.videoTime(), Time::TimePoint(0));

    std::function<void(Time::TimePoint)> videoTimeUpdater = [&](Time::TimePoint t) {
        clockSync.setVideoTime(t);
    };

    const double renderedPts = 3.5;
    videoTimeUpdater(Time::TimePoint::fromSeconds(renderedPts));

    EXPECT_EQ(clockSync.videoTime(), Time::TimePoint::fromSeconds(renderedPts));
}

TEST(AVSyncIntegrationTest, VideoTimeUpdaterEnablesAVSyncAfterFirstFrame) {
    ClockSync clockSync;

    std::function<void(Time::TimePoint)> videoTimeUpdater = [&](Time::TimePoint t) {
        clockSync.setVideoTime(t);
    };

    clockSync.setAudioTime(Time::TimePoint::fromSeconds(4.0));

    {
        double diff = clockSync.audioTime().second() - clockSync.videoTime().second();
        EXPECT_EQ(ClockSync::evaluateAVSync(diff), AVSyncAction::Drop);
    }

    videoTimeUpdater(Time::TimePoint::fromSeconds(3.97));

    {
        double diff = clockSync.audioTime().second() - clockSync.videoTime().second();
        EXPECT_EQ(ClockSync::evaluateAVSync(diff), AVSyncAction::Render);
    }
}

TEST(AVSyncIntegrationTest, VideoTimeUpdaterNullSafeWhenNotSet) {
    std::function<void(Time::TimePoint)> videoTimeUpdater;
    EXPECT_FALSE(static_cast<bool>(videoTimeUpdater));

    if (videoTimeUpdater) {
        videoTimeUpdater(Time::TimePoint::fromSeconds(1.0));
    }
}


TEST(AVSyncIntegrationTest, DiscardFrameHasCorrectFlag) {
    auto frame = makeVideoFrame(2.0, true);
    EXPECT_TRUE(frame->isDiscard);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 2.0);
}

TEST(AVSyncIntegrationTest, NormalFrameHasDiscardFlagFalse) {
    auto frame = makeVideoFrame(2.0, false);
    EXPECT_FALSE(frame->isDiscard);
}


TEST(AVSyncIntegrationTest, ClockSyncActualThresholdIs40ms) {
    EXPECT_EQ(ClockSync::evaluateAVSync(0.04), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.04), AVSyncAction::Render);

    EXPECT_EQ(ClockSync::evaluateAVSync(0.041), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.041), AVSyncAction::Hold);
}

TEST(AVSyncIntegrationTest, ClockSyncEarlyToleranceReducesHold) {
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.05, 1.0, 0.0), AVSyncAction::Hold);

    EXPECT_EQ(ClockSync::evaluateAVSync(-0.05, 1.0, ClockSync::kEarlyTolerance),
              AVSyncAction::Hold);

    EXPECT_EQ(ClockSync::evaluateAVSync(-0.041, 1.0, 0.0), AVSyncAction::Hold);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.041, 1.0, ClockSync::kEarlyTolerance),
              AVSyncAction::Render);
}


TEST(AVSyncIntegrationTest, SeekEpochInvalidatesOldFrames) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(5.0), SeekStrategy::SeekToKeyframe);
    uint64_t epoch = ctx.currentEpoch();

    std::ignore = ctx.beginSeek(CTime(10.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.isEpochStale(epoch));
}

TEST(AVSyncIntegrationTest, SeekCompleteResetsClockSyncVideoTime) {
    ClockSync clockSync;
    SeekContext ctx;

    clockSync.setVideoTime(Time::TimePoint::fromSeconds(3.0));
    EXPECT_EQ(clockSync.videoTime(), Time::TimePoint::fromSeconds(3.0));

    std::ignore = ctx.beginSeek(CTime(10.0), SeekStrategy::SeekToKeyframe);
    clockSync.reset();
    EXPECT_EQ(clockSync.videoTime(), Time::TimePoint(0));

    std::ignore = ctx.completeSeek();
    EXPECT_FALSE(ctx.isSeekPending());
}


TEST(AVSyncIntegrationTest, KeyframeSeekAlignsMasterClockToTarget) {
    ClockSync clockSync;
    clockSync.setHasAudio(true);
    clockSync.setVideoFps(30.0);

    constexpr double kTarget = 1.434;

    double audioRenderOffset = 0.0;
    auto simulatePerformKeyframeSeekFix = [&](double target) {
        audioRenderOffset = target;
        clockSync.seekTo(Time::TimePoint::fromSeconds(target));
    };

    simulatePerformKeyframeSeekFix(kTarget);

    EXPECT_NEAR(clockSync.masterTime().second(), kTarget, 1e-4);
    EXPECT_NEAR(audioRenderOffset, kTarget, 1e-9);
    EXPECT_NEAR(clockSync.videoFps(), 30.0, 0.01);

    clockSync.setVideoTime(Time::TimePoint::fromSeconds(kTarget + 0.033));
    const double diff = clockSync.masterTime().second() - clockSync.videoTime().second();
    EXPECT_EQ(ClockSync::evaluateAVSync(diff, 1.0, ClockSync::kEarlyTolerance),
              AVSyncAction::Render);
}

TEST(AVSyncIntegrationTest, KeyframeSeekBuggyBehaviorWouldHoldVideo) {
    ClockSync clockSync;
    clockSync.setHasAudio(true);

    constexpr double kTarget = 1.434;
    clockSync.reset();
    EXPECT_NEAR(clockSync.masterTime().second(), 0.0, 1e-4);

    clockSync.setVideoTime(Time::TimePoint::fromSeconds(kTarget));
    const double diff = clockSync.masterTime().second() - clockSync.videoTime().second();
    EXPECT_EQ(ClockSync::evaluateAVSync(diff, 1.0, ClockSync::kEarlyTolerance), AVSyncAction::Hold);
}
