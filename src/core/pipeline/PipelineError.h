//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <functional>
#include <optional>

#include "Error.h"
#include "PipelineEvent.h"

namespace slark {

struct PipelineError {
    Error error = Error::Success;
    int32_t nativeCode = 0;
};

using PipelineEventCallback = std::function<void(PipelineEvent, std::optional<PipelineError>)>;

}  // namespace slark
