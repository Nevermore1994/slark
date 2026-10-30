//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "WatermarkGate.h"

#include <gtest/gtest.h>

#include <atomic>
#include <limits>
#include <thread>
#include <vector>

using namespace slark;

namespace {

struct ActionCounts {
    int close = 0;
    int open = 0;
    int interrupt = 0;
    int resume = 0;

    [[nodiscard]] int total() const noexcept {
        return close + open + interrupt + resume;
    }
};

// Records every gate action so the trigger semantics (edge vs level) can be asserted.
template <typename Level>
class GateFixture {
 public:
    explicit GateFixture(WatermarkConfig<Level> config)
        : gate_(config,
                typename WatermarkGate<Level>::Actions{
                    .closeGate = [this] { ++actions_.close; },
                    .openGate = [this] { ++actions_.open; },
                    .interrupt = [this] { ++actions_.interrupt; },
                    .resume = [this] { ++actions_.resume; },
                }) {}

    [[nodiscard]] WatermarkGate<Level>& gate() noexcept {
        return gate_;
    }
    [[nodiscard]] ActionCounts& actions() noexcept {
        return actions_;
    }

 private:
    ActionCounts actions_;
    WatermarkGate<Level> gate_;
};

}  // namespace

// ---- pure predicate -------------------------------------------------------

TEST(WatermarkGatePredicateTest, ClosesAtHighAndReopensAtLow) {
    EXPECT_FALSE(shouldCloseGate(7, 2, 8, false));
    EXPECT_TRUE(shouldCloseGate(8, 2, 8, false));
    EXPECT_TRUE(shouldCloseGate(9, 2, 8, false));

    EXPECT_TRUE(shouldCloseGate(3, 2, 8, true));
    EXPECT_FALSE(shouldCloseGate(2, 2, 8, true));
    EXPECT_FALSE(shouldCloseGate(1, 2, 8, true));
}

TEST(WatermarkGatePredicateTest, DeadZoneIsStickyInBothDirections) {
    for (int pending = 3; pending < 8; ++pending) {
        EXPECT_TRUE(shouldCloseGate(pending, 2, 8, true)) << "pending=" << pending;
        EXPECT_FALSE(shouldCloseGate(pending, 2, 8, false)) << "pending=" << pending;
    }
}

TEST(WatermarkGatePredicateTest, InvalidWatermarksKeepCurrentState) {
    EXPECT_TRUE(shouldCloseGate(0, 8, 2, true));
    EXPECT_FALSE(shouldCloseGate(0, 8, 2, false));
    EXPECT_TRUE(shouldCloseGate(100, 8, 2, true));
    EXPECT_FALSE(shouldCloseGate(100, 8, 2, false));

    // Equal watermarks leave no band, so the state never changes either.
    EXPECT_TRUE(shouldCloseGate(5, 5, 5, true));
    EXPECT_FALSE(shouldCloseGate(5, 5, 5, false));
}

TEST(WatermarkGatePredicateTest, AdjacentWatermarksLeaveNoDeadZone) {
    EXPECT_TRUE(shouldCloseGate(4, 3, 4, false));
    EXPECT_TRUE(shouldCloseGate(4, 3, 4, true));
    EXPECT_FALSE(shouldCloseGate(3, 3, 4, true));
    EXPECT_FALSE(shouldCloseGate(3, 3, 4, false));
}

TEST(WatermarkGatePredicateTest, ExtremeLevelsDoNotOverflow) {
    constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
    constexpr int32_t kMin = std::numeric_limits<int32_t>::min();

    EXPECT_TRUE(shouldCloseGate(kMax, kMin + 1, kMax, false));
    EXPECT_TRUE(shouldCloseGate(-1, 2, 8, true) == false);
    EXPECT_FALSE(shouldCloseGate(kMin, 2, 8, false));
    EXPECT_FALSE(shouldCloseGate(kMin, 2, 8, true));
}

TEST(WatermarkGatePredicateTest, WorksForDoubleLevels) {
    EXPECT_FALSE(shouldCloseGate(29.9, 2.0, 30.0, false));
    EXPECT_TRUE(shouldCloseGate(30.0, 2.0, 30.0, false));
    EXPECT_TRUE(shouldCloseGate(29.9, 2.0, 30.0, true));
    EXPECT_FALSE(shouldCloseGate(2.0, 2.0, 30.0, true));
}

// ---- gate -----------------------------------------------------------------

TEST(WatermarkGateTest, CloseIsEdgeTriggeredOpenIsLevelSignal) {
    GateFixture<int32_t> fixture({2, 8});
    auto& gate = fixture.gate();

    gate.update(8);
    gate.update(9);
    gate.update(100);
    EXPECT_EQ(fixture.actions().close, 1) << "closing must not repeat while already closed";

    gate.update(1);
    gate.update(2);
    gate.update(0);
    EXPECT_EQ(fixture.actions().open, 3) << "capacity may be re-announced on every low sample";
}

TEST(WatermarkGateTest, DeadZoneDoesNotReopenGate) {
    GateFixture<int32_t> fixture({2, 8});
    auto& gate = fixture.gate();

    gate.update(8);
    ASSERT_TRUE(gate.isClosed());

    for (int pending = 7; pending > 2; --pending) {
        gate.update(pending);
        EXPECT_TRUE(gate.isClosed()) << "pending=" << pending;
    }
    EXPECT_EQ(fixture.actions().open, 0);
}

TEST(WatermarkGateTest, InvalidWatermarksNeverToggle) {
    GateFixture<int32_t> fixture({8, 2});
    auto& gate = fixture.gate();

    for (int pending : {0, 4, 8, 100}) {
        gate.update(pending);
    }
    EXPECT_FALSE(gate.isClosed());
    EXPECT_EQ(fixture.actions().total(), 0);
}

TEST(WatermarkGateTest, OperationPauseBlocksWatermarkSampling) {
    GateFixture<int32_t> fixture({2, 8});
    auto& gate = fixture.gate();

    gate.forceClose();
    EXPECT_TRUE(gate.isForced());
    EXPECT_TRUE(gate.isPaused());
    EXPECT_FALSE(gate.isClosed()) << "an operation pause is not a watermark suspension";
    EXPECT_EQ(fixture.actions().interrupt, 1);
    EXPECT_EQ(fixture.actions().close, 0);

    gate.update(1);
    gate.update(100);
    EXPECT_EQ(fixture.actions().total(), 1) << "watermark samples must not drive the gate";
}

TEST(WatermarkGateTest, ForceCloseIsIdempotent) {
    GateFixture<int32_t> fixture({2, 8});
    auto& gate = fixture.gate();

    gate.forceClose();
    gate.forceClose();
    gate.forceClose();
    EXPECT_EQ(fixture.actions().interrupt, 1);
}

TEST(WatermarkGateTest, ForceOpenOnlyFiresWhenForced) {
    GateFixture<int32_t> fixture({2, 8});
    auto& gate = fixture.gate();

    gate.forceOpen();
    EXPECT_EQ(fixture.actions().total(), 0) << "releasing an unpaused gate must be a no-op";

    gate.forceClose();
    gate.forceOpen();
    EXPECT_EQ(fixture.actions().resume, 1);
    EXPECT_EQ(fixture.actions().open, 1) << "capacity is announced once the pause is released";
    EXPECT_FALSE(gate.isPaused());
}

TEST(WatermarkGateTest, ResetDropsWatermarkStateButKeepsOperationPause) {
    GateFixture<int32_t> fixture({2, 8});
    auto& gate = fixture.gate();

    gate.forceClose();
    gate.update(100);
    gate.reset();

    EXPECT_TRUE(gate.isForced()) << "reset must not silently release an operation pause";
    EXPECT_FALSE(gate.isClosed());
    EXPECT_EQ(fixture.actions().interrupt, 1);
    EXPECT_EQ(fixture.actions().open, 0) << "reset must not touch the delegate";
}

TEST(WatermarkGateTest, WatermarkSurvivesAFullOperationCycle) {
    GateFixture<int32_t> fixture({2, 8});
    auto& gate = fixture.gate();

    gate.update(8);
    ASSERT_TRUE(gate.isClosed());

    gate.forceClose();
    EXPECT_FALSE(gate.isClosed()) << "the operation path takes over the watermark state";

    gate.forceOpen();
    EXPECT_FALSE(gate.isClosed());

    gate.update(8);
    EXPECT_TRUE(gate.isClosed()) << "the watermark path still works after an operation cycle";
    EXPECT_EQ(fixture.actions().close, 2);
}

TEST(WatermarkGateTest, WorksWithoutAnyCallback) {
    WatermarkGate<int32_t> gate({2, 8}, {});

    gate.update(8);
    EXPECT_TRUE(gate.isClosed());
    gate.update(1);
    EXPECT_FALSE(gate.isClosed());
    gate.forceClose();
    gate.forceOpen();
    EXPECT_FALSE(gate.isPaused());
}

// The decoder worker samples the gate while the owner thread forces it, so both flags are
// atomic: hammer both paths and assert the observable state stays consistent.
TEST(WatermarkGateTest, ConcurrentSamplingAndForcingStayConsistent) {
    WatermarkGate<int32_t> gate({2, 8}, {});
    std::atomic<bool> stop{false};

    std::thread sampler([&gate, &stop] {
        int32_t level = 0;
        while (!stop.load()) {
            gate.update(level);
            level = (level + 1) % 16;
        }
    });

    std::thread operation([&gate, &stop] {
        while (!stop.load()) {
            gate.forceClose();
            gate.forceOpen();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);
    sampler.join();
    operation.join();

    // Whatever interleaving happened, an operation pause always wins over a watermark.
    gate.forceClose();
    EXPECT_TRUE(gate.isPaused());
    gate.forceOpen();
    EXPECT_FALSE(gate.isForced());
}
