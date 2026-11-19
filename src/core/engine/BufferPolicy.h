//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "FlowConfig.h"

namespace slark::engine {

// Stateless: the caller keeps the rebuffering flag, which is what makes the dead band work.
class BufferPolicy {
 public:
    explicit BufferPolicy(const FlowConfig& config = {}) noexcept : config_(config) {}

    [[nodiscard]] const FlowConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] bool canStart(double aheadSec) const noexcept {
        return aheadSec >= config_.buffering.startBufferSec;
    }

    [[nodiscard]] bool shouldRebuffer(double aheadSec, bool rebuffering) const noexcept {
        if (rebuffering) {
            return aheadSec < config_.buffering.rebufferExitSec;
        }
        return aheadSec < config_.buffering.rebufferEnterSec;
    }

 private:
    FlowConfig config_;
};

}  // namespace slark::engine
