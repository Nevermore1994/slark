//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "pipeline/SeekContext.h"

#include <gtest/gtest.h>
#include <thread>

using namespace slark;

// MARK: - 初始状态

TEST(SeekContextTest, InitialStateIsIdle) {
    SeekContext ctx;
    EXPECT_FALSE(ctx.isSeekPending());
    EXPECT_FALSE(ctx.isVideoSeeked());
    EXPECT_FALSE(ctx.isAudioSeeked());
    EXPECT_EQ(ctx.currentEpoch(), 0u);
    EXPECT_FALSE(ctx.targetSeekTime().has_value());
}

// MARK: - beginSeek

TEST(SeekContextTest, BeginSeekSetsPendingAndEpoch) {
    SeekContext ctx;
    bool superseded = ctx.beginSeek(CTime(5.0), SeekStrategy::SeekToKeyframe);
    EXPECT_FALSE(superseded);
    EXPECT_TRUE(ctx.isSeekPending());
    EXPECT_EQ(ctx.currentEpoch(), 1u);
    ASSERT_TRUE(ctx.targetSeekTime().has_value());
    EXPECT_DOUBLE_EQ(*ctx.targetSeekTime(), 5.0);
    EXPECT_EQ(ctx.strategy(), SeekStrategy::SeekToKeyframe);
}

TEST(SeekContextTest, BeginSeekReturnsTrueWhenSuperseding) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(3.0), SeekStrategy::SeekToKeyframe);
    bool superseded = ctx.beginSeek(CTime(7.0), SeekStrategy::FastForwardInGOP);
    EXPECT_TRUE(superseded);
    EXPECT_EQ(ctx.currentEpoch(), 2u);
    ASSERT_TRUE(ctx.targetSeekTime().has_value());
    EXPECT_DOUBLE_EQ(*ctx.targetSeekTime(), 7.0);
    EXPECT_EQ(ctx.strategy(), SeekStrategy::FastForwardInGOP);
}

TEST(SeekContextTest, BeginSeekResetsTrackSeekedFlags) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    ctx.markVideoSeeked();
    ctx.markAudioSeeked();
    EXPECT_TRUE(ctx.isVideoSeeked());
    EXPECT_TRUE(ctx.isAudioSeeked());

    std::ignore = ctx.beginSeek(CTime(2.0), SeekStrategy::SeekToKeyframe);
    EXPECT_FALSE(ctx.isVideoSeeked());
    EXPECT_FALSE(ctx.isAudioSeeked());
}

// MARK: - completeSeek

TEST(SeekContextTest, CompleteSeekClearsState) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(5.0), SeekStrategy::SeekToKeyframe);
    bool completed = ctx.completeSeek();
    EXPECT_TRUE(completed);
    EXPECT_FALSE(ctx.isSeekPending());
    EXPECT_FALSE(ctx.targetSeekTime().has_value());
}

TEST(SeekContextTest, CompleteSeekReturnsFalseWhenNotPending) {
    SeekContext ctx;
    bool completed = ctx.completeSeek();
    EXPECT_FALSE(completed);
}

TEST(SeekContextTest, CompleteSeekIdempotentOnSecondCall) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(5.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.completeSeek());
    EXPECT_FALSE(ctx.completeSeek());
}

// MARK: - epoch 管理

TEST(SeekContextTest, EpochIncreasesMonotonically) {
    SeekContext ctx;
    for (uint64_t i = 1; i <= 5; ++i) {
        std::ignore = ctx.beginSeek(CTime(static_cast<double>(i)), SeekStrategy::SeekToKeyframe);
        EXPECT_EQ(ctx.currentEpoch(), i);
    }
}

TEST(SeekContextTest, IsEpochStaleDetectsNewSeek) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    uint64_t captured = ctx.currentEpoch();
    EXPECT_FALSE(ctx.isEpochStale(captured));

    std::ignore = ctx.beginSeek(CTime(2.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.isEpochStale(captured));
    EXPECT_FALSE(ctx.isEpochStale(ctx.currentEpoch()));
}

// MARK: - tryMarkVideoSeeked CAS 抢占

TEST(SeekContextTest, TryMarkVideoSeekedFirstCallReturnsTrue) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.tryMarkVideoSeeked());
    EXPECT_TRUE(ctx.isVideoSeeked());
}

TEST(SeekContextTest, TryMarkVideoSeekedSecondCallReturnsFalse) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    EXPECT_TRUE(ctx.tryMarkVideoSeeked());
    EXPECT_FALSE(ctx.tryMarkVideoSeeked());
}

TEST(SeekContextTest, TryMarkVideoSeekedConcurrentOnlyOneWins) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);

    std::atomic<int32_t> winCount{0};
    auto task = [&] {
        if (ctx.tryMarkVideoSeeked()) {
            winCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::thread t1(task);
    std::thread t2(task);
    std::thread t3(task);
    t1.join();
    t2.join();
    t3.join();

    EXPECT_EQ(winCount.load(), 1);
}

// MARK: - isAllTracksSeeked 组合逻辑

TEST(SeekContextTest, AllTracksSeekedVideoOnly) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    EXPECT_FALSE(ctx.isAllTracksSeeked(true, false));
    ctx.markVideoSeeked();
    EXPECT_TRUE(ctx.isAllTracksSeeked(true, false));
}

TEST(SeekContextTest, AllTracksSeekedAudioOnly) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    EXPECT_FALSE(ctx.isAllTracksSeeked(false, true));
    ctx.markAudioSeeked();
    EXPECT_TRUE(ctx.isAllTracksSeeked(false, true));
}

TEST(SeekContextTest, AllTracksSeekedBothTracks) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(1.0), SeekStrategy::SeekToKeyframe);
    EXPECT_FALSE(ctx.isAllTracksSeeked(true, true));
    ctx.markVideoSeeked();
    EXPECT_FALSE(ctx.isAllTracksSeeked(true, true));
    ctx.markAudioSeeked();
    EXPECT_TRUE(ctx.isAllTracksSeeked(true, true));
}

TEST(SeekContextTest, AllTracksSeekedNoTracks) {
    SeekContext ctx;
    // 无音视频轨道时，始终视为已 seek 完成
    EXPECT_TRUE(ctx.isAllTracksSeeked(false, false));
}

// MARK: - GOP 范围更新

TEST(SeekContextTest, UpdateGOPRangeStoresValues) {
    SeekContext ctx;
    std::ignore = ctx.beginSeek(CTime(5.0), SeekStrategy::FastForwardInGOP);
    ctx.updateGOPRange(4.5, 6.0);
    EXPECT_DOUBLE_EQ(ctx.gopKeyframePts(), 4.5);
    EXPECT_DOUBLE_EQ(ctx.gopEndPts(), 6.0);
}
