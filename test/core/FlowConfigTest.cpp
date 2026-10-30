//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include "FlowConfig.h"

using namespace slark;

// FlowConfig{} is the single default every source type gets; the values are public API
// (mirrored by the Android and iOS config objects), so they are pinned here.
TEST(FlowConfigTest, DefaultConfigIsValid) {
    const FlowConfig cfg;
    EXPECT_TRUE(cfg.isValid());
    EXPECT_DOUBLE_EQ(cfg.source.highWatermarkSec, 10.0);
    EXPECT_DOUBLE_EQ(cfg.source.lowWatermarkSec, 3.0);
    EXPECT_DOUBLE_EQ(cfg.buffering.startBufferSec, 2.0);
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferEnterSec, 3.0);
    EXPECT_DOUBLE_EQ(cfg.buffering.rebufferExitSec, 4.5);
}

TEST(FlowConfigTest, EverySliceIsValidInTheDefaultConfig) {
    const FlowConfig cfg;
    EXPECT_TRUE(cfg.source.isValid());
    EXPECT_TRUE(cfg.buffering.isValid());
}

// A broken slice must invalidate the aggregate, otherwise a bad value reaches a module that
// never validates it.
TEST(FlowConfigTest, InvalidSliceMakesWholeConfigInvalid) {
    {
        auto cfg = FlowConfig{};
        cfg.source.lowWatermarkSec = cfg.source.highWatermarkSec + 1.0;
        EXPECT_FALSE(cfg.source.isValid());
        EXPECT_FALSE(cfg.isValid());
    }
    {
        auto cfg = FlowConfig{};
        cfg.buffering.rebufferExitSec = cfg.buffering.rebufferEnterSec - 0.5;
        EXPECT_FALSE(cfg.buffering.isValid());
        EXPECT_FALSE(cfg.isValid());
    }
}

// Cross-slice constraint: production stops at the high watermark, so a rebuffer exit level at
// or above it can never be reached and rebuffering would never end.
TEST(FlowConfigTest, RebufferExitMustStayBelowSourceHighWatermark) {
    auto cfg = FlowConfig{};
    ASSERT_LT(cfg.buffering.rebufferExitSec, cfg.source.highWatermarkSec);
    EXPECT_TRUE(cfg.isValid());

    cfg.buffering.rebufferExitSec = 8.0;
    EXPECT_TRUE(cfg.isValid());

    cfg.buffering.rebufferExitSec = cfg.source.highWatermarkSec - 0.001;
    EXPECT_TRUE(cfg.isValid());

    cfg.buffering.rebufferExitSec = cfg.source.highWatermarkSec;
    EXPECT_FALSE(cfg.isValid()) << "exit level at the high watermark never recovers";

    cfg.buffering.rebufferExitSec = 11.25;
    EXPECT_FALSE(cfg.isValid()) << "exit level above the high watermark never recovers";
}

TEST(FlowConfigTest, SourceWatermarkBoundaries) {
    auto cfg = FlowConfig{};
    cfg.source.lowWatermarkSec = 0.0;
    EXPECT_TRUE(cfg.source.isValid());

    cfg.source.lowWatermarkSec = cfg.source.highWatermarkSec;
    EXPECT_FALSE(cfg.source.isValid());

    cfg.source.lowWatermarkSec = -1.0;
    EXPECT_FALSE(cfg.source.isValid());
}

TEST(FlowConfigTest, BufferingConfigBoundaries) {
    BufferingConfig cfg;
    cfg.startBufferSec = 0.0;
    EXPECT_TRUE(cfg.isValid());

    cfg.rebufferEnterSec = 0.0;
    EXPECT_TRUE(cfg.isValid());

    // Equal thresholds mean no dead band, which is allowed.
    cfg.rebufferEnterSec = 3.0;
    cfg.rebufferExitSec = 3.0;
    EXPECT_TRUE(cfg.isValid());

    cfg.rebufferExitSec = 2.99;
    EXPECT_FALSE(cfg.isValid());

    cfg.rebufferExitSec = 4.5;
    cfg.rebufferEnterSec = -1.0;
    EXPECT_FALSE(cfg.isValid());

    cfg.rebufferEnterSec = 3.0;
    cfg.startBufferSec = -1.0;
    EXPECT_FALSE(cfg.isValid());
}
