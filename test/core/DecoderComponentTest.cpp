//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include "codec/DecoderComponent.h"

using namespace slark;

// The hysteresis is a pure decision function, so every backpressure edge case is
// exercised here without needing a real codec or a running worker thread.
class DecodeBackpressureTest : public ::testing::Test {
 protected:
    static constexpr int32_t kLow = 2;
    static constexpr int32_t kHigh = 8;
};

TEST_F(DecodeBackpressureTest, PausesWhenPendingReachesHighWatermark) {
    EXPECT_FALSE(shouldPauseFeeding(7, kLow, kHigh, false));
    EXPECT_TRUE(shouldPauseFeeding(8, kLow, kHigh, false));
    EXPECT_TRUE(shouldPauseFeeding(9, kLow, kHigh, false));
}

TEST_F(DecodeBackpressureTest, ResumesOnlyWhenPendingDropsToLowWatermark) {
    // Resuming happens at <= low, not at < high: this is the hysteresis.
    EXPECT_TRUE(shouldPauseFeeding(3, kLow, kHigh, true));
    EXPECT_FALSE(shouldPauseFeeding(2, kLow, kHigh, true));
    EXPECT_FALSE(shouldPauseFeeding(1, kLow, kHigh, true));
}

TEST_F(DecodeBackpressureTest, DeadZoneBetweenWatermarksKeepsCurrentState) {
    // The classic bug: a single if/else-if leaves (low, high) unhandled and a paused
    // decoder stays paused forever. The state must be sticky in both directions.
    for (int32_t pending = kLow + 1; pending < kHigh; ++pending) {
        EXPECT_TRUE(shouldPauseFeeding(pending, kLow, kHigh, true))
            << "must stay paused at pending=" << pending;
        EXPECT_FALSE(shouldPauseFeeding(pending, kLow, kHigh, false))
            << "must stay running at pending=" << pending;
    }
}

TEST_F(DecodeBackpressureTest, DoesNotOscillateAtBoundary) {
    // Feeding -> pause -> consumption -> resume must not flip-flop around the edges.
    bool paused = shouldPauseFeeding(kHigh, kLow, kHigh, false);
    ASSERT_TRUE(paused);
    // Still at high after producing one more frame: stays paused.
    EXPECT_TRUE(shouldPauseFeeding(kHigh, kLow, kHigh, paused));
    // Consumed down into the dead zone: still paused (no premature resume).
    EXPECT_TRUE(shouldPauseFeeding(kLow + 1, kLow, kHigh, paused));
    // Reached the low watermark: resumes.
    paused = shouldPauseFeeding(kLow, kLow, kHigh, paused);
    EXPECT_FALSE(paused);
    // A frame arrives but we are still at/below low: stays running.
    EXPECT_FALSE(shouldPauseFeeding(kLow, kLow, kHigh, paused));
}

TEST_F(DecodeBackpressureTest, InvalidWatermarksKeepCurrentState) {
    // Misconfigured high <= low must not cause rapid toggling.
    EXPECT_TRUE(shouldPauseFeeding(0, 8, 2, true));
    EXPECT_FALSE(shouldPauseFeeding(0, 8, 2, false));
    EXPECT_TRUE(shouldPauseFeeding(100, 8, 2, true));
    EXPECT_FALSE(shouldPauseFeeding(100, 8, 2, false));

    // Equal watermarks are equally invalid.
    EXPECT_TRUE(shouldPauseFeeding(5, 5, 5, true));
    EXPECT_FALSE(shouldPauseFeeding(5, 5, 5, false));
}

TEST_F(DecodeBackpressureTest, NegativePendingIsTreatedAsEmpty) {
    // A negative count is nonsense but must not crash or keep the decoder paused.
    EXPECT_FALSE(shouldPauseFeeding(-1, kLow, kHigh, false));
    EXPECT_FALSE(shouldPauseFeeding(-1, kLow, kHigh, true));
}

TEST_F(DecodeBackpressureTest, AdjacentWatermarksLeaveNoDeadZone) {
    // low=3, high=4: there is no room for a dead zone, so the state is driven purely
    // by which side of each watermark the queue sits on.
    EXPECT_TRUE(shouldPauseFeeding(4, 3, 4, false));
    EXPECT_TRUE(shouldPauseFeeding(4, 3, 4, true));
    EXPECT_FALSE(shouldPauseFeeding(3, 3, 4, true));
    EXPECT_FALSE(shouldPauseFeeding(3, 3, 4, false));
}

TEST(DecoderComponentConfigTest, DefaultsAreSane) {
    DecoderComponentConfig config;
    EXPECT_EQ(config.backpressure.lowWatermarkFrames, 2);
    EXPECT_EQ(config.backpressure.highWatermarkFrames, 8);
    EXPECT_LT(config.backpressure.lowWatermarkFrames, config.backpressure.highWatermarkFrames);
    EXPECT_TRUE(config.backpressure.isValid());
    EXPECT_FALSE(config.pendingFrameCount);
    EXPECT_FALSE(config.isAudio);
}

// Watermarks derived from SPS must satisfy low < high, otherwise the hysteresis above
// degrades to "keep current state" and the decoder never reacts to the queue.
TEST(DecoderComponentConfigTest, ReorderDerivedWatermarksStayOrdered) {
    for (uint8_t reorder = 1; reorder <= 16; ++reorder) {
        const int32_t low = std::max(2, static_cast<int32_t>(reorder));
        const int32_t high = std::max(8, std::max(reorder * 2, reorder + 4));
        EXPECT_LT(low, high) << "reorder=" << static_cast<int>(reorder);
    }
}
