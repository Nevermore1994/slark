//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "BufferPolicy.h"

#include <gtest/gtest.h>

using slark::FlowConfig;
using slark::engine::BufferPolicy;

namespace {

FlowConfig withStart(double startSec) {
    FlowConfig cfg;
    cfg.buffering.startBufferSec = startSec;
    return cfg;
}

}  // namespace

TEST(BufferPolicyTest, StartThresholdBoundary) {
    const BufferPolicy policy(withStart(2.0));
    EXPECT_FALSE(policy.canStart(1.999));
    EXPECT_TRUE(policy.canStart(2.0));
    EXPECT_TRUE(policy.canStart(5.0));
}

TEST(BufferPolicyTest, ZeroStartBufferAlwaysStarts) {
    const BufferPolicy policy(withStart(0.0));
    EXPECT_TRUE(policy.canStart(0.0));
}

TEST(BufferPolicyTest, EntersRebufferingBelowTarget) {
    const BufferPolicy policy;
    EXPECT_FALSE(policy.shouldRebuffer(3.0, false));
    EXPECT_TRUE(policy.shouldRebuffer(2.99, false));
}

TEST(BufferPolicyTest, LeavesRebufferingAtExitLevel) {
    const BufferPolicy policy;
    EXPECT_TRUE(policy.shouldRebuffer(4.49, true));
    EXPECT_FALSE(policy.shouldRebuffer(4.5, true));
}

TEST(BufferPolicyTest, DeadBandKeepsTheCurrentState) {
    const BufferPolicy policy;
    for (double ahead : {3.0, 3.5, 4.0, 4.49}) {
        EXPECT_FALSE(policy.shouldRebuffer(ahead, false)) << ahead;
    }
    for (double ahead : {3.0, 3.5, 4.0, 4.49}) {
        EXPECT_TRUE(policy.shouldRebuffer(ahead, true)) << ahead;
    }
}
