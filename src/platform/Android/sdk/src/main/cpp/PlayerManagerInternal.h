//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "JNINames.h"
#include "Manager.hpp"
#include "Player.h"
#include "PlayerObserver.h"

#include <memory>
#include <string>

namespace slark {

class PlayerObserver : public IPlayerObserver {
public:
    PlayerObserver() = default;

    void notifyPlayedTime(std::string_view playerId, double time) override;
    void notifyPlayerState(std::string_view playerId, PlayerState state) override;
    void notifyPlayerEvent(std::string_view playerId,
                           slark::PlayerEvent event,
                           std::string value) override;

private:
    static constexpr const char* kPlayerManagerClass = JNI::JavaClass::SlarkPlayerManager;
    static constexpr const char* kPlayerStateClass = JNI::JavaClass::SlarkPlayerState;
    static constexpr const char* kPlayerEventClass = JNI::JavaClass::SlarkPlayerEvent;
};

using PlayerManager = Manager<Player>;
using PlayerObserverManager = Manager<PlayerObserver>;

enum class Action : int32_t { Prepare = 0, Play, Pause, Release };

/// 异步创建解码资源（Share Context 延迟到首次使用时初始化）
void asyncCreateDecoderResource(const std::string& playerId) noexcept;

} // namespace slark
