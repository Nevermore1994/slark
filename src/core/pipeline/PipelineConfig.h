//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>

namespace slark {

enum class MediaSourceType : uint8_t {
    Local,
    Network,
    Live,
};

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
    bool isLiveSource = false;

    [[nodiscard]] double rebufferTargetSec() const noexcept {
        return startBufferSec * rebufferScale;
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

struct PipelineFlowConfig {
    SourceBackpressureConfig source;
    DecoderBackpressureConfig audioDecoder{.lowWatermarkFrames = 4, .highWatermarkFrames = 16};
    DecoderBackpressureConfig videoDecoder;
    BufferingConfig buffering;
    CatchUpConfig catchUp;

    // Rebuffer target must stay below the high watermark, otherwise rebuffering never exits.
    [[nodiscard]] bool isValid() const noexcept {
        return source.isValid() && audioDecoder.isValid() && videoDecoder.isValid() &&
               buffering.isValid() && catchUp.isValid() &&
               buffering.rebufferTargetSec() < source.highWatermarkSec;
    }

    [[nodiscard]] static PipelineFlowConfig forSource(MediaSourceType type) noexcept {
        PipelineFlowConfig cfg;
        switch (type) {
            case MediaSourceType::Local:
                cfg.source.lowWatermarkSec = 2.0;
                cfg.source.highWatermarkSec = 30.0;
                cfg.buffering.startBufferSec = 0.0;
                break;
            case MediaSourceType::Network:
                break;
            case MediaSourceType::Live:
                cfg.source.lowWatermarkSec = 1.0;
                cfg.source.highWatermarkSec = 5.0;
                cfg.buffering.startBufferSec = 1.0;
                cfg.buffering.isLiveSource = true;
                cfg.audioDecoder = {.lowWatermarkFrames = 2, .highWatermarkFrames = 8};
                cfg.videoDecoder = {.lowWatermarkFrames = 1, .highWatermarkFrames = 4};
                cfg.catchUp.maxDropPerVsync = 4;
                break;
        }
        return cfg;
    }
};

}  // namespace slark
