//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>

namespace slark {

// Pipeline 内部阶段，用于错误定位
enum class PipelineStage : uint8_t {
    Demux,
    AudioDecode,
    VideoDecode,
    AudioRender,
    VideoRender,
    BackPressure,
    Seek,
};

} // namespace slark
