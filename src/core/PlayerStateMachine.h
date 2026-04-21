//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Event.h"
#include "Player.h"

#include <expected>
#include <optional>
#include <string_view>

namespace slark {

/**
 * @brief 播放器状态机，封装 PlayerState 的合法转换逻辑。
 *        从 PlayerImpl 中提取，便于独立测试和维护。
 */
class PlayerStateMachine {
public:
    PlayerStateMachine() = default;
    ~PlayerStateMachine() = default;

    // 判断 from → to 是否为合法状态转换
    [[nodiscard]] static bool isValidTransition(PlayerState from, PlayerState to) noexcept;

    // 从事件类型推断目标状态（无法推断时返回 unexpected）
    [[nodiscard]] static std::expected<PlayerState, bool> getStateFromEvent(
        EventType event) noexcept;
};

} // namespace slark
