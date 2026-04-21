//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Error.h"
#include "PipelineEvent.h"
#include "PipelineStage.h"

#include <functional>
#include <optional>
#include <string>

namespace slark {

// Pipeline 错误载荷，携带在 AudioDecoderFailed / VideoDecoderFailed / ReadError 事件中
struct PipelineError {
    PipelineEvent event;
    PipelineStage stage;
    Error error = Error::Success;
    int32_t code = 0;
    std::string message;
};

// Pipeline 对外事件回调类型
using PipelineEventCallback = std::function<void(PipelineEvent, std::optional<PipelineError>)>;

// Error → PipelineError 统一转换
[[nodiscard]] inline PipelineError toPipelineError(Error error,
                                                   PipelineEvent event,
                                                   PipelineStage stage,
                                                   std::string message = {}) {
    return PipelineError{event, stage, error, static_cast<int32_t>(error), std::move(message)};
}

} // namespace slark
namespace slark
