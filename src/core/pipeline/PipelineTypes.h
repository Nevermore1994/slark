//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <functional>
#include <optional>

#include "Error.h"

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

struct PipelineError {
    Error error = Error::Success;
    int32_t nativeCode = 0;
};

using PipelineEventCallback = std::function<void(PipelineEvent, std::optional<PipelineError>)>;

}  // namespace slark
