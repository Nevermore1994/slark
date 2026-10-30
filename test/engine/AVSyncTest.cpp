//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AVSync.h"

#include <gtest/gtest.h>

#include "SourceSupport.h"

using namespace slark;
using namespace slark::engine;

namespace {

AVFrameRefPtr videoFrame(double pts) {
    auto frame = std::make_shared<AVFrame>(AVFrameType::Video);
    frame->pts = static_cast<int64_t>(pts * 90000);
    frame->timeScale = 90000;
    auto info = std::make_shared<VideoFrameInfo>();
    frame->info = std::move(info);
    return frame;
}

std::deque<AVFrameRefPtr> framesAt(std::initializer_list<double> ptsList) {
    std::deque<AVFrameRefPtr> queue;
    for (const auto pts : ptsList) {
        queue.push_back(videoFrame(pts));
    }
    return queue;
}

}  // namespace

// The four ClockSyncTest::EvaluateAVSync* cases, restated through the pacer with the front
// frame encoding the same diff. Keeps the ported thresholds honest.
TEST(AVSyncTest, EarlyTolerancePreventsFalseHold) {
    // evaluateAVSync(-0.06, rate 1.0, tolerance 0.03) renders.
    const auto decision =
        AVSync::decide(1.00, framesAt({1.06}), {.playbackRate = 1.0, .earlyToleranceSec = 0.03});
    EXPECT_EQ(decision.action, AVSync::Action::Present);
}

TEST(AVSyncTest, EarlyToleranceStillHoldsWhenFarAhead) {
    // evaluateAVSync(-0.15, rate 1.0, tolerance 0.03) holds.
    const auto decision =
        AVSync::decide(1.00, framesAt({1.15}), {.playbackRate = 1.0, .earlyToleranceSec = 0.03});
    EXPECT_EQ(decision.action, AVSync::Action::Hold);
}

TEST(AVSyncTest, ZeroEarlyToleranceMatchesClockSync) {
    // evaluateAVSync(-0.03, tolerance 0) renders; evaluateAVSync(-0.15, tolerance 0) holds.
    const auto render = AVSync::decide(1.00, framesAt({1.03}), {.earlyToleranceSec = 0.0});
    EXPECT_EQ(render.action, AVSync::Action::Present);

    const auto hold = AVSync::decide(1.00, framesAt({1.15}), {.earlyToleranceSec = 0.0});
    EXPECT_EQ(hold.action, AVSync::Action::Hold);
}

TEST(AVSyncTest, HighRateWithEarlyTolerance) {
    // evaluateAVSync(-0.10, rate 2.0, tolerance 0.04) renders; (-0.15, ...) holds.
    AVSync::Config config{.playbackRate = 2.0, .earlyToleranceSec = 0.04};
    const auto render = AVSync::decide(1.00, framesAt({1.10}), config);
    EXPECT_EQ(render.action, AVSync::Action::Present);

    const auto hold = AVSync::decide(1.00, framesAt({1.15}), config);
    EXPECT_EQ(hold.action, AVSync::Action::Hold);
}

TEST(AVSyncTest, SlightlyStaleFramePresents) {
    const auto decision = AVSync::decide(1.00, framesAt({0.98}), {});
    EXPECT_EQ(decision.action, AVSync::Action::Present);
    EXPECT_EQ(decision.dropCount, 0);
}

TEST(AVSyncTest, DropCountNeverExceedsMaxPerVsync) {
    // Twenty late frames; only 5 may be dropped in one vsync.
    std::deque<AVFrameRefPtr> queue;
    for (int i = 0; i < 20; ++i) {
        queue.push_back(videoFrame(0.5 + i * 0.01));
    }
    AVSync::Config config{.maxDropPerVsync = 5};
    const auto decision = AVSync::decide(2.0, queue, config);
    EXPECT_EQ(decision.action, AVSync::Action::Drop);
    EXPECT_EQ(decision.dropCount, 5);
}

TEST(AVSyncTest, FarAheadFramesRenderInsteadOfHold) {
    // The frame is ≥10s ahead of the clock: a hold would never end, so render resets.
    const auto decision = AVSync::decide(5.0, framesAt({20.0}), {.earlyToleranceSec = 0.0});
    EXPECT_EQ(decision.action, AVSync::Action::Present);
}

TEST(AVSyncTest, EmptyQueueHolds) {
    const auto decision = AVSync::decide(1.0, {}, {});
    EXPECT_EQ(decision.action, AVSync::Action::Hold);
}
