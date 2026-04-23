//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "EpochFilter.h"

#include "SeekCoordinator.h"

namespace slark {

bool EpochFilter::shouldDiscard(const SeekCoordinator& coordinator, double framePts) noexcept {
    if (auto seekTime = coordinator.activeSeekTime()) {
        if (framePts < *seekTime) {
            return true;
        }
    }
    return false;
}

bool EpochFilter::shouldBreak(const SeekCoordinator& coordinator, uint64_t capturedEpoch) noexcept {
    return capturedEpoch != coordinator.seekEpoch();
}

} // namespace slark
