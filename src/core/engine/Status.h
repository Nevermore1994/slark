//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <cstdint>

namespace slark::engine {

enum class Status : uint8_t {
    Idle,
    Opening,
    Buffering,
    Seeking,
    Ready,
    Ended,
    Failed,
};

}  // namespace slark::engine
