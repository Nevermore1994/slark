//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

namespace slark::engine {

// Samples are rebased so the window starts at 0; invalid means the whole media.
struct PlaybackRange {
    double startSec = 0.0;
    double endSec = 0.0;

    [[nodiscard]] bool valid() const noexcept {
        return endSec > startSec;
    }
};

}  // namespace slark::engine
