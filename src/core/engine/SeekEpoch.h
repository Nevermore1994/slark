//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <cstdint>

namespace slark::engine {

// Identifies one seek. Pipelines and sources stamp frames with it and drop frames of older epochs.
using SeekEpoch = uint64_t;
inline constexpr SeekEpoch kInvalidSeekEpoch = 0;

}  // namespace slark::engine
