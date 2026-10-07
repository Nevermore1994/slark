//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>

namespace slark {

struct SourceBackpressureConfig {
    double lowWatermarkSec = 3.0;
    double highWatermarkSec = 10.0;

    [[nodiscard]] bool isValid() const noexcept {
        return lowWatermarkSec >= 0.0 && lowWatermarkSec < highWatermarkSec;
    }
};

struct DecoderBackpressureConfig {
    int32_t lowWatermarkFrames = 2;
    int32_t highWatermarkFrames = 8;

    [[nodiscard]] bool isValid() const noexcept {
        return lowWatermarkFrames >= 0 && lowWatermarkFrames < highWatermarkFrames;
    }
};

struct BufferingConfig {
    double startBufferSec = 2.0;
    double rebufferScale = 1.5;
    int32_t starvationThreshold = 5;

    [[nodiscard]] double rebufferTargetSec() const noexcept {
        return startBufferSec * rebufferScale;
    }

    // Rebuffering exits above where it entered, otherwise a queue oscillating around the
    // target flaps BufferingStart/BufferingEnd on every sample.
    [[nodiscard]] double rebufferExitSec() const noexcept {
        return rebufferTargetSec() * rebufferScale;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return startBufferSec >= 0.0 && rebufferScale >= 1.0 && starvationThreshold > 0;
    }
};

struct CatchUpConfig {
    int32_t maxDropPerVsync = 8;
    int64_t seekFastForwardMs = 300;

    [[nodiscard]] bool isValid() const noexcept {
        return maxDropPerVsync > 0 && seekFastForwardMs > 0;
    }
};

struct FlowConfig {
    SourceBackpressureConfig source;
    DecoderBackpressureConfig audioDecoder{.lowWatermarkFrames = 4, .highWatermarkFrames = 16};
    DecoderBackpressureConfig videoDecoder;
    BufferingConfig buffering;
    CatchUpConfig catchUp;

    // Production stops at the high watermark, so the level a rebuffer needs to exit must stay
    // below it, otherwise rebuffering never exits.
    [[nodiscard]] bool isValid() const noexcept {
        return source.isValid() && audioDecoder.isValid() && videoDecoder.isValid() &&
               buffering.isValid() && catchUp.isValid() &&
               buffering.rebufferExitSec() < source.highWatermarkSec;
    }
};

}  // namespace slark
