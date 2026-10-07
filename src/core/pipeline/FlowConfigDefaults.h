//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <optional>

#include "FlowConfig.h"
#include "MediaSourceType.h"

namespace slark::pipeline {

class FlowConfigDefaults {
 public:
    FlowConfigDefaults() = delete;

    [[nodiscard]] static FlowConfig forSource(MediaSourceType type) noexcept {
        FlowConfig cfg;
        switch (type) {
            case MediaSourceType::Local:
                cfg.source.lowWatermarkSec = 2.0;
                cfg.source.highWatermarkSec = 15.0;
                cfg.buffering.startBufferSec = 0.0;
                break;
            case MediaSourceType::Network:
                break;
            case MediaSourceType::Live:
                cfg.source.lowWatermarkSec = 1.0;
                cfg.source.highWatermarkSec = 5.0;
                cfg.buffering.startBufferSec = 1.0;
                cfg.audioDecoder = {.lowWatermarkFrames = 2, .highWatermarkFrames = 8};
                cfg.videoDecoder = {.lowWatermarkFrames = 1, .highWatermarkFrames = 4};
                cfg.catchUp.maxDropPerVsync = 4;
                break;
        }
        return cfg;
    }

    [[nodiscard]] static FlowConfig resolve(const std::optional<FlowConfig>& overrideConfig,
                                            MediaSourceType type) noexcept {
        if (overrideConfig && overrideConfig->isValid()) {
            return *overrideConfig;
        }
        return forSource(type);
    }
};

}
