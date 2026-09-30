//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "pipeline/BufferingTracker.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

using namespace slark;
using ::testing::_;
using ::testing::Eq;
using ::testing::NiceMock;

class MockBufferingTrackerDelegate : public IBufferingTrackerDelegate {
 public:
    MOCK_METHOD(void, onPrebufferComplete, (), (noexcept, override));
    MOCK_METHOD(void,
                onBufferEvent,
                (PipelineEvent, std::optional<PipelineError>),
                (noexcept, override));
};

namespace {
constexpr MediaSourceType kNetwork = MediaSourceType::Network;
constexpr MediaSourceType kLocal = MediaSourceType::Local;

BufferingConfig networkBuffering() noexcept {
    return PipelineFlowConfig::forSource(kNetwork).buffering;
}
}  // namespace

TEST(BufferingTrackerTest, StartPrebufferingFiresImmediatelyWhenStartBufferIsZero) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    auto cfg = networkBuffering();
    cfg.startBufferSec = 0.0;

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);

    BufferingTracker tracker(cfg, delegate);
    tracker.startPrebuffering();
    EXPECT_FALSE(tracker.isPrebuffering());
}

TEST(BufferingTrackerTest, StartPrebufferingWaitsUntilDurationReached) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    BufferingTracker tracker(networkBuffering(), delegate);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);

    tracker.startPrebuffering();
    ASSERT_TRUE(tracker.isPrebuffering());

    tracker.update(1.0);
    EXPECT_TRUE(tracker.isPrebuffering());

    tracker.update(3.0);
    EXPECT_FALSE(tracker.isPrebuffering());
}

TEST(BufferingTrackerTest, PrebufferSuppressesBufferingEvents) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    auto cfg = networkBuffering();
    cfg.startBufferSec = 5.0;

    EXPECT_CALL(delegate, onBufferEvent(_, _)).Times(0);

    BufferingTracker tracker(cfg, delegate);
    tracker.startPrebuffering();
    tracker.update(0.0);
    EXPECT_TRUE(tracker.isPrebuffering());
    EXPECT_FALSE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, RebufferingStartsBelowTargetAfterReady) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    auto cfg = networkBuffering();
    ASSERT_DOUBLE_EQ(cfg.rebufferTargetSec(), 3.0);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, Eq(std::nullopt))).Times(1);

    BufferingTracker tracker(cfg, delegate);
    tracker.startPrebuffering();
    tracker.update(5.0);
    ASSERT_FALSE(tracker.isPrebuffering());

    tracker.update(1.0);
    EXPECT_TRUE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, RebufferingEndsWhenDurationRecovers) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    const auto cfg = networkBuffering();
    ASSERT_DOUBLE_EQ(cfg.rebufferTargetSec(), 3.0);
    ASSERT_DOUBLE_EQ(cfg.rebufferExitSec(), 4.5);

    BufferingTracker tracker(cfg, delegate);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, Eq(std::nullopt))).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingEnd, Eq(std::nullopt))).Times(1);

    tracker.startPrebuffering();
    tracker.update(5.0);
    tracker.update(1.0);
    ASSERT_TRUE(tracker.isRebuffering());

    tracker.update(4.5);
    EXPECT_FALSE(tracker.isRebuffering());
}

// Regression: entering and exiting on the same threshold made the state flap whenever the
// queue sat exactly on the target. The exit threshold is now strictly above the entry one.
TEST(BufferingTrackerTest, RebufferingHasADeadZoneAboveTheTarget) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    BufferingTracker tracker(networkBuffering(), delegate);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, Eq(std::nullopt))).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingEnd, Eq(std::nullopt))).Times(1);

    tracker.startPrebuffering();
    tracker.update(5.0);
    tracker.update(1.0);
    ASSERT_TRUE(tracker.isRebuffering());

    for (double duration : {3.0, 3.5, 4.49}) {
        tracker.update(duration);
        EXPECT_TRUE(tracker.isRebuffering()) << "duration=" << duration;
    }

    tracker.update(4.5);
    EXPECT_FALSE(tracker.isRebuffering());
}

// A local source has startBufferSec = 0, so rebufferTargetSec() collapses to 0. Entering
// requires strictly less than the target, which is unreachable for a clamped duration:
// the old `<=` fired BufferingStart and BufferingEnd within the same sample.
TEST(BufferingTrackerTest, LocalSourceNeverFlapsOnZeroTarget) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    const auto cfg = PipelineFlowConfig::forSource(kLocal).buffering;
    ASSERT_DOUBLE_EQ(cfg.rebufferTargetSec(), 0.0);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(_, _)).Times(0);

    BufferingTracker tracker(cfg, delegate);
    tracker.startPrebuffering();

    for (double duration : {0.0, 0.0, 0.0}) {
        tracker.update(duration);
        EXPECT_FALSE(tracker.isRebuffering());
    }
}

// The exact entry boundary: strictly below the target enters, exactly on it does not.
TEST(BufferingTrackerTest, RebufferingEntryBoundaryIsExclusive) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    BufferingTracker tracker(networkBuffering(), delegate);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(_, _)).Times(0);

    tracker.startPrebuffering();
    tracker.update(5.0);
    tracker.update(3.0);
    EXPECT_FALSE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, RebufferingIsNotRetriggeredWhileActive) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    BufferingTracker tracker(networkBuffering(), delegate);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, _)).Times(1);

    tracker.startPrebuffering();
    tracker.update(5.0);
    tracker.update(1.0);
    tracker.update(1.0);
    tracker.update(0.5);
    EXPECT_TRUE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, SourceEofEndsRebuffering) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    BufferingTracker tracker(networkBuffering(), delegate);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, _)).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingEnd, _)).Times(1);

    tracker.startPrebuffering();
    tracker.update(5.0);
    tracker.update(1.0);
    ASSERT_TRUE(tracker.isRebuffering());

    tracker.setSourceEof();
    EXPECT_FALSE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, SourceEofPreventsFurtherRebufferingStart) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    BufferingTracker tracker(networkBuffering(), delegate);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(_, _)).Times(0);

    tracker.startPrebuffering();
    tracker.update(5.0);
    tracker.setSourceEof();
    tracker.update(0.0);
    EXPECT_FALSE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, StarvationTriggersRebufferingAfterThreshold) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    auto cfg = networkBuffering();

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, Eq(std::nullopt))).Times(1);

    BufferingTracker tracker(cfg, delegate);
    tracker.startPrebuffering();
    tracker.update(5.0);

    for (int32_t i = 0; i < cfg.starvationThreshold; ++i) {
        tracker.reportStarvation(false);
    }
    EXPECT_TRUE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, StarvationBelowThresholdDoesNotRebuffer) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    auto cfg = networkBuffering();

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(_, _)).Times(0);

    BufferingTracker tracker(cfg, delegate);
    tracker.startPrebuffering();
    tracker.update(5.0);

    for (int32_t i = 0; i < cfg.starvationThreshold - 1; ++i) {
        tracker.reportStarvation(false);
    }
    EXPECT_FALSE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, StarvationHonorsConfiguredThreshold) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    auto cfg = networkBuffering();
    cfg.starvationThreshold = 1;

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, _)).Times(1);

    BufferingTracker tracker(cfg, delegate);
    tracker.startPrebuffering();
    tracker.update(5.0);

    tracker.reportStarvation(false);
    EXPECT_TRUE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, StarvationIgnoredForLocalSource) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    BufferingTracker tracker(networkBuffering(), delegate);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(_, _)).Times(0);

    tracker.startPrebuffering();
    tracker.update(5.0);

    for (int32_t i = 0; i < 10; ++i) {
        tracker.reportStarvation(true);
    }
    EXPECT_FALSE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, ResetStarvationCountDefersRebuffering) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    auto cfg = networkBuffering();

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(_, _)).Times(0);

    BufferingTracker tracker(cfg, delegate);
    tracker.startPrebuffering();
    tracker.update(5.0);

    tracker.reportStarvation(false);
    tracker.reportStarvation(false);
    tracker.resetStarvationCount();
    tracker.reportStarvation(false);
    tracker.reportStarvation(false);
    EXPECT_FALSE(tracker.isRebuffering());
}

TEST(BufferingTrackerTest, ResetClearsAllState) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    BufferingTracker tracker(networkBuffering(), delegate);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, _)).Times(1);

    tracker.startPrebuffering();
    tracker.update(5.0);
    tracker.update(1.0);
    ASSERT_TRUE(tracker.isRebuffering());

    tracker.reset();
    EXPECT_FALSE(tracker.isPrebuffering());
    EXPECT_FALSE(tracker.isRebuffering());
}

// A local source has startBufferSec = 0, so readiness fires during startPrebuffering()
// rather than on the first sample.
TEST(BufferingTrackerTest, LocalSourceIsReadyImmediately) {
    NiceMock<MockBufferingTrackerDelegate> delegate;
    auto cfg = PipelineFlowConfig::forSource(kLocal).buffering;
    ASSERT_DOUBLE_EQ(cfg.startBufferSec, 0.0);

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);

    BufferingTracker tracker(cfg, delegate);
    tracker.startPrebuffering();
    EXPECT_FALSE(tracker.isPrebuffering());
}
