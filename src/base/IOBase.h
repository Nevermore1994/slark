//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>

namespace slark {

enum class IOState : uint8_t {
    Unknown,
    Normal,
    Pause,
    Error,
    EndOfFile,
    Closed,
};

}
