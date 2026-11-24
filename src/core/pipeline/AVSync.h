//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <deque>
#include <memory>

#include "AVFrame.hpp"

namespace slark::engine {

class AVSync {
 public:
    enum class Action : uint8_t {
        Present,
        Hold,
        Drop,
    };

    struct Decision {
        Action action = Action::Hold;
        // Drop only: frames to discard this vsync, never above maxDropPerVsync.
        int32_t dropCount = 0;
    };

    struct Config {
        double playbackRate = 1.0;
        double earlyToleranceSec = 0.0;
        int32_t maxDropPerVsync = 8;
    };

    // queue holds decoded frames in presentation order; clockNowSec is the media position.
    [[nodiscard]] static Decision decide(double clockNowSec,
                                       const std::deque<AVFrameRefPtr>& queue,
                                       const Config& config) noexcept;

    static constexpr double kMinThreshold = 0.04;
    static constexpr double kMaxThreshold = 10.0;
};

}  // namespace slark::engine
