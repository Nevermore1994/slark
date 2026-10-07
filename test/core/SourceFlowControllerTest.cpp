//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "pipeline/SourceFlowController.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "pipeline/FlowConfigDefaults.h"

using namespace slark;
using namespace slark::pipeline;
using ::testing::NiceMock;

class MockSourceFlowDelegate : public ISourceFlowDelegate {
 public:
    MOCK_METHOD(void, closeFetchGate, (), (noexcept, override));
    MOCK_METHOD(void, openFetchGate, (), (noexcept, override));
    MOCK_METHOD(void, interruptStream, (), (noexcept, override));
    MOCK_METHOD(void, resumeStream, (), (noexcept, override));
};

namespace {
constexpr MediaSourceType kNetwork = MediaSourceType::Network;

SourceBackpressureConfig networkSource() noexcept {
    return FlowConfigDefaults::forSource(kNetwork).source;
}
}  // namespace

// The watermark path suspends production instead of pausing: pausing is destructive
// (closes connections, cancels timers, invalidates buffered HLS data) while a watermark
// only means "enough buffered for now".
TEST(SourceFlowControllerTest, HighWatermarkSuspendsWithoutPausingStream) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, closeFetchGate()).Times(1);
    EXPECT_CALL(delegate, interruptStream()).Times(0);
    EXPECT_CALL(delegate, resumeStream()).Times(0);

    flow.update(12.0);
    EXPECT_TRUE(flow.isProductionSuspended());
    EXPECT_FALSE(flow.isOperationPaused());
}

TEST(SourceFlowControllerTest, LowWatermarkNotifiesCapacityWithoutResumingStream) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, closeFetchGate()).Times(1);
    flow.update(12.0);

    EXPECT_CALL(delegate, openFetchGate()).Times(1);
    EXPECT_CALL(delegate, resumeStream()).Times(0);

    flow.update(1.0);
    EXPECT_FALSE(flow.isProductionSuspended());
    EXPECT_FALSE(flow.isOperationPaused());
}

// Hysteresis: the zone between the watermarks must not flip the state either way.
TEST(SourceFlowControllerTest, DeadZoneKeepsState) {
    NiceMock<MockSourceFlowDelegate> delegate;
    auto cfg = networkSource();
    ASSERT_DOUBLE_EQ(cfg.lowWatermarkSec, 3.0);
    ASSERT_DOUBLE_EQ(cfg.highWatermarkSec, 10.0);

    SourceFlowController flow(cfg, delegate);

    EXPECT_CALL(delegate, closeFetchGate()).Times(0);
    EXPECT_CALL(delegate, openFetchGate()).Times(0);
    for (double duration : {3.1, 6.0, 9.9}) {
        flow.update(duration);
        EXPECT_FALSE(flow.isProductionSuspended());
    }

    EXPECT_CALL(delegate, closeFetchGate()).Times(1);
    flow.update(10.0);
    ASSERT_TRUE(flow.isProductionSuspended());

    EXPECT_CALL(delegate, closeFetchGate()).Times(0);
    EXPECT_CALL(delegate, openFetchGate()).Times(0);
    for (double duration : {9.9, 6.0, 3.1}) {
        flow.update(duration);
        EXPECT_TRUE(flow.isProductionSuspended());
    }
}

// The delegate call is edge triggered: a stream that is already suspended must not be
// suspended again on every sample above the high watermark.
TEST(SourceFlowControllerTest, SuspendIsEdgeTriggered) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, closeFetchGate()).Times(1);
    flow.update(12.0);
    flow.update(15.0);
    flow.update(20.0);
    EXPECT_TRUE(flow.isProductionSuspended());
}

// Every sample below the low watermark may re-notify: capacity is a level signal for
// sources whose production is gated per unit.
TEST(SourceFlowControllerTest, CapacityNotificationRepeatsBelowLowWatermark) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, openFetchGate()).Times(3);
    flow.update(1.0);
    flow.update(2.0);
    flow.update(0.5);
    EXPECT_FALSE(flow.isProductionSuspended());
}

// Operational pauses interrupt production immediately and must not be released by a
// watermark sample, otherwise a seek could resume the source mid-operation.
TEST(SourceFlowControllerTest, OperationalPauseIsNotReleasedByWatermark) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, interruptStream()).Times(1);
    flow.pauseForOperation();
    EXPECT_TRUE(flow.isOperationPaused());

    EXPECT_CALL(delegate, resumeStream()).Times(0);
    EXPECT_CALL(delegate, openFetchGate()).Times(0);
    EXPECT_CALL(delegate, closeFetchGate()).Times(0);
    flow.update(1.0);
    flow.update(12.0);
    EXPECT_TRUE(flow.isOperationPaused());
}

TEST(SourceFlowControllerTest, OperationalPauseIsIdempotent) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, interruptStream()).Times(1);
    flow.pauseForOperation();
    flow.pauseForOperation();
    flow.pauseForOperation();

    EXPECT_CALL(delegate, resumeStream()).Times(1);
    flow.resumeForOperation();
    flow.resumeForOperation();
    EXPECT_FALSE(flow.isOperationPaused());
}

TEST(SourceFlowControllerTest, OperationalResumeNotifiesCapacity) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, interruptStream()).Times(1);
    flow.pauseForOperation();

    EXPECT_CALL(delegate, resumeStream()).Times(1);
    EXPECT_CALL(delegate, openFetchGate()).Times(1);
    flow.resumeForOperation();
    EXPECT_FALSE(flow.isProductionSuspended());
}

// A seek flush resets source state while the operation pause remains paired with the
// delegate until the seek has installed its new position.
TEST(SourceFlowControllerTest, ResetDuringOperationPreservesPauseUntilResume) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, interruptStream()).Times(1);
    flow.pauseForOperation();
    ASSERT_TRUE(flow.isOperationPaused());

    flow.reset();
    EXPECT_TRUE(flow.isOperationPaused());
    EXPECT_FALSE(flow.isProductionSuspended());

    EXPECT_CALL(delegate, resumeStream()).Times(1);
    EXPECT_CALL(delegate, openFetchGate()).Times(1);
    flow.resumeForOperation();
    EXPECT_FALSE(flow.isOperationPaused());
}

TEST(SourceFlowControllerTest, SourceEofStopsRegulation) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, closeFetchGate()).Times(0);
    EXPECT_CALL(delegate, openFetchGate()).Times(0);
    EXPECT_CALL(delegate, interruptStream()).Times(0);
    EXPECT_CALL(delegate, resumeStream()).Times(0);

    flow.setSourceEof();
    flow.update(12.0);
    flow.update(0.0);
}

TEST(SourceFlowControllerTest, ResetClearsSourceEof) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);
    flow.setSourceEof();

    EXPECT_CALL(delegate, closeFetchGate()).Times(1);
    flow.reset();
    flow.update(12.0);
    EXPECT_TRUE(flow.isProductionSuspended());
}

TEST(SourceFlowControllerTest, OperationAfterSuspendPausesImmediately) {
    NiceMock<MockSourceFlowDelegate> delegate;
    SourceFlowController flow(networkSource(), delegate);

    EXPECT_CALL(delegate, closeFetchGate()).Times(1);
    flow.update(12.0);
    ASSERT_TRUE(flow.isProductionSuspended());

    EXPECT_CALL(delegate, interruptStream()).Times(1);
    flow.pauseForOperation();
    EXPECT_TRUE(flow.isOperationPaused());
    EXPECT_FALSE(flow.isProductionSuspended());
}
