//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Event.h"
#include "Player.h"
#include "pipeline/PipelineEvent.h"

#include <expected>
#include <optional>

namespace slark {

class PlayerStateMachine {
    PlayerStateMachine() = default;
    ~PlayerStateMachine() = default;

public:
    [[nodiscard]] static bool isValidTransition(PlayerState from, PlayerState to) noexcept;

    [[nodiscard]] static std::expected<PlayerState, bool> getStateFromEvent(
        EventType event) noexcept;

    [[nodiscard]] static std::expected<PlayerState, bool> getStateFromPipelineEvent(
        PipelineEvent event) noexcept;
};

} // namespace slark
