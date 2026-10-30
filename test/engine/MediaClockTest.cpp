//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "MediaClock.h"

#include <gtest/gtest.h>

#include <chrono>

#include "ManualScheduler.h"

using namespace std::chrono_literals;
using slark::engine::MediaClock;
using slark::test::ManualScheduler;
using slark::Time::TimePoint;

namespace {

TimePoint sec(double s) {
    return TimePoint::fromSeconds(s);
}

class MediaClockTest : public ::testing::Test {
 protected:
    ManualScheduler wall;
    MediaClock clock{wall};
};

}  // namespace

TEST_F(MediaClockTest, StoppedClockDoesNotAdvance) {
    clock.seekTo(sec(5));
    wall.advance(3s);
    EXPECT_EQ(clock.now(), sec(5));
}

TEST_F(MediaClockTest, RunningClockAdvancesWithWallTime) {
    clock.seekTo(sec(5));
    clock.start();
    wall.advance(2s);
    EXPECT_EQ(clock.now(), sec(7));
}

TEST_F(MediaClockTest, StopFreezesAtTheCurrentPosition) {
    clock.start();
    wall.advance(1s);
    clock.stop();
    wall.advance(5s);
    EXPECT_EQ(clock.now(), sec(1));
    EXPECT_FALSE(clock.running());
}

TEST_F(MediaClockTest, StartAfterStopResumesWithoutCountingTheGap) {
    clock.start();
    wall.advance(1s);
    clock.stop();
    wall.advance(5s);
    clock.start();
    wall.advance(1s);
    EXPECT_EQ(clock.now(), sec(2));
}

TEST_F(MediaClockTest, StartAndStopAreIdempotent) {
    clock.start();
    wall.advance(1s);
    clock.start();
    wall.advance(1s);
    EXPECT_EQ(clock.now(), sec(2));
    clock.stop();
    clock.stop();
    EXPECT_EQ(clock.now(), sec(2));
}

TEST_F(MediaClockTest, DoubleRateAdvancesTwiceAsFast) {
    clock.start();
    clock.setRate(2.0);
    wall.advance(1s);
    EXPECT_EQ(clock.now(), sec(2));
}

TEST_F(MediaClockTest, SetRateDoesNotJumpThePosition) {
    clock.start();
    wall.advance(3s);
    const auto before = clock.now();
    clock.setRate(2.0);
    EXPECT_EQ(clock.now(), before);
    wall.advance(1s);
    EXPECT_EQ(clock.now(), before + slark::Time::TimeDelta::fromMilliSeconds(2000ms));
}

TEST_F(MediaClockTest, SetRateWhileStoppedKeepsPosition) {
    clock.seekTo(sec(4));
    clock.setRate(0.5);
    EXPECT_EQ(clock.now(), sec(4));
    clock.start();
    wall.advance(2s);
    EXPECT_EQ(clock.now(), sec(5));
    EXPECT_DOUBLE_EQ(clock.rate(), 0.5);
}

TEST_F(MediaClockTest, SeekToTakesEffectImmediatelyWhileRunning) {
    clock.start();
    wall.advance(1s);
    clock.seekTo(sec(30));
    EXPECT_EQ(clock.now(), sec(30));
    wall.advance(1s);
    EXPECT_EQ(clock.now(), sec(31));
}

TEST_F(MediaClockTest, AudioAnchorSmallStepBackIsIgnored) {
    clock.start();
    wall.advance(1s);
    clock.anchor(sec(1) - slark::Time::TimeDelta(5'000));
    EXPECT_EQ(clock.now(), sec(1));
}

TEST_F(MediaClockTest, AudioAnchorLargeStepBackIsTaken) {
    clock.start();
    wall.advance(1s);
    clock.anchor(sec(1) - slark::Time::TimeDelta(50'000));
    EXPECT_EQ(clock.now(), sec(1) - slark::Time::TimeDelta(50'000));
}

TEST_F(MediaClockTest, AudioAnchorStepBackExactlyAtTheLimitIsTaken) {
    clock.start();
    wall.advance(1s);
    clock.anchor(sec(1) - slark::Time::TimeDelta(10'000));
    EXPECT_EQ(clock.now(), sec(1) - slark::Time::TimeDelta(10'000));
}

TEST_F(MediaClockTest, AudioAnchorForwardIsTaken) {
    clock.start();
    wall.advance(1s);
    clock.anchor(sec(1) + slark::Time::TimeDelta(3'000));
    EXPECT_EQ(clock.now(), sec(1) + slark::Time::TimeDelta(3'000));
}

TEST_F(MediaClockTest, ResetReturnsToZeroStopped) {
    clock.setRate(2.0);
    clock.seekTo(sec(9));
    clock.start();
    clock.reset();
    wall.advance(1s);
    EXPECT_EQ(clock.now(), sec(0));
    EXPECT_FALSE(clock.running());
    EXPECT_DOUBLE_EQ(clock.rate(), 1.0);
}
