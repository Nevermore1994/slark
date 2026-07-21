//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>

namespace slark {

enum class PipelineEvent : uint8_t {
    DemuxHeaderParsed,
    PipelineReady,
    FirstFrameRendered,
    AudioDecoderFailed,
    VideoDecoderFailed,
    BufferingStart,
    BufferingEnd,
    ReadError,
    SeekCompleted,
    DecodeError,
    RenderEnd
};

} // namespace slark
