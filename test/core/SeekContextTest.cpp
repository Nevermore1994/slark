#include "SeekContext.h"

#include <gtest/gtest.h>

using namespace slark;

TEST(SeekContextTest, InitiallyNoSeekPending) {
    SeekContext ctx;
    EXPECT_FALSE(ctx.isSeekPending());
    EXPECT_FALSE(ctx.targetSeekTime().has_value());
}

TEST(SeekContextTest, BeginSeekMarksPending) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(10.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.isSeekPending());
    EXPECT_TRUE(ctx.targetSeekTime().has_value());
    EXPECT_DOUBLE_EQ(*ctx.targetSeekTime(), 10.0);
}

TEST(SeekContextTest, CompleteSeekClearsPending) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(5.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.completeSeek());
    EXPECT_FALSE(ctx.isSeekPending());
}

TEST(SeekContextTest, CompleteSeekWithoutBeginReturnsFalse) {
    SeekContext ctx;
    EXPECT_FALSE(ctx.completeSeek());
}

TEST(SeekContextTest, EpochIncrementsOnBeginSeek) {
    SeekContext ctx;
    auto epoch1 = ctx.currentEpoch();
    (void)ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    auto epoch2 = ctx.currentEpoch();
    EXPECT_NE(epoch1, epoch2);
}

TEST(SeekContextTest, EpochStaleness) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    auto epoch = ctx.currentEpoch();
    EXPECT_FALSE(ctx.isEpochStale(epoch));
    (void)ctx.beginSeek(CTime(2.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.isEpochStale(epoch));
}

TEST(SeekContextTest, UpdateGOPRange) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(5.0), SeekStrategy::FastForwardInGOP);
    ctx.updateGOPRange(3.0, 8.0);
    EXPECT_DOUBLE_EQ(ctx.gopKeyframePts(), 3.0);
    EXPECT_DOUBLE_EQ(ctx.gopEndPts(), 8.0);
}

TEST(SeekContextTest, UpdateTarget) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(5.0), SeekStrategy::SeekToKeyframe);
    ctx.updateTarget(7.0);
    EXPECT_DOUBLE_EQ(*ctx.targetSeekTime(), 7.0);
}

TEST(SeekContextTest, StrategyAccess) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(0.0), SeekStrategy::FastForwardInGOP);
    EXPECT_EQ(ctx.strategy(), SeekStrategy::FastForwardInGOP);
}

TEST(SeekContextTest, TrackSeekState) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(0.0), SeekStrategy::SeekToKeyframe);
    EXPECT_FALSE(ctx.isVideoSeeked());
    EXPECT_FALSE(ctx.isAudioSeeked());
    ctx.markVideoSeeked();
    EXPECT_TRUE(ctx.isVideoSeeked());
    EXPECT_FALSE(ctx.isAudioSeeked());
    ctx.markAudioSeeked();
    EXPECT_TRUE(ctx.isVideoSeeked());
    EXPECT_TRUE(ctx.isAudioSeeked());
}

TEST(SeekContextTest, TryMarkVideoSeekedOnce) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(0.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.tryMarkVideoSeeked());
    EXPECT_FALSE(ctx.tryMarkVideoSeeked());
}

TEST(SeekContextTest, AllTracksSeeked) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(0.0), SeekStrategy::SeekToKeyframe);
    EXPECT_FALSE(ctx.isAllTracksSeeked(true, true));
    ctx.markVideoSeeked();
    EXPECT_FALSE(ctx.isAllTracksSeeked(true, true));
    ctx.markAudioSeeked();
    EXPECT_TRUE(ctx.isAllTracksSeeked(true, true));
}

TEST(SeekContextTest, AllTracksSeekedAudioOnly) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(0.0), SeekStrategy::SeekToKeyframe);
    EXPECT_FALSE(ctx.isAllTracksSeeked(false, true));
    ctx.markAudioSeeked();
    EXPECT_TRUE(ctx.isAllTracksSeeked(false, true));
}

TEST(SeekContextTest, MaxSkipFrames) {
    SeekContext ctx;
    EXPECT_EQ(ctx.maxSkipFrames(), 0);
    ctx.setMaxSkipFrames(5);
    EXPECT_EQ(ctx.maxSkipFrames(), 5);
}

TEST(SeekContextTest, BeginSeekWhilePending) {
    SeekContext ctx;
    (void)ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.beginSeek(CTime(2.0), SeekStrategy::SeekToKeyframe));
}
