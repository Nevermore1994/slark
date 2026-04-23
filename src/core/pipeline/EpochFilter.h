//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Time.hpp"

#include <cstdint>
#include <optional>

namespace slark {

class SeekCoordinator;

class EpochFilter {
public:
    EpochFilter() = delete;

    [[nodiscard]] static bool shouldDiscard(const SeekCoordinator& coordinator,
                                            double framePts) noexcept;

    [[nodiscard]] static bool shouldBreak(const SeekCoordinator& coordinator,
                                          uint64_t capturedEpoch) noexcept;
};

} // namespace slark
