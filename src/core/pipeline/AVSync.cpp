//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AVSync.h"

#include <algorithm>

namespace slark::engine {

namespace {

AVSync::Action evaluateDiff(double diffSec, const AVSync::Config& config) noexcept {
    const double scaledMin = AVSync::kMinThreshold * config.playbackRate;
    if (diffSec > scaledMin) {
        return AVSync::Action::Drop;
    }
    if (diffSec < -(scaledMin + config.earlyToleranceSec)) {
        return -diffSec >= AVSync::kMaxThreshold ? AVSync::Action::Present : AVSync::Action::Hold;
    }
    return AVSync::Action::Present;
}

}  // namespace

AVSync::Decision AVSync::decide(double clockNowSec,
                            const std::deque<AVFrameRefPtr>& queue,
                            const Config& config) noexcept {
    if (queue.empty()) {
        return {Action::Hold, 0};
    }
    // clock minus frame time: positive means the frame is stale, negative means it is early.
    const auto first = evaluateDiff(clockNowSec - queue.front()->ptsTime(), config);
    if (first != Action::Drop) {
        return {first, 0};
    }
    int32_t droppable = 0;
    for (const auto& frame : queue) {
        if (evaluateDiff(clockNowSec - frame->ptsTime(), config) != Action::Drop) {
            break;
        }
        ++droppable;
        if (droppable >= config.maxDropPerVsync) {
            break;
        }
    }
    return {Action::Drop, std::max(droppable, 1)};
}

}  // namespace slark::engine
