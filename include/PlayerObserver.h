//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Player.h"

namespace slark {

struct IPlayerObserver {
    virtual void notifyPlayedTime(std::string_view playerId, double time) = 0;

    virtual void notifyPlayerState(std::string_view playerId, PlayerState state) = 0;

    virtual void notifyPlayerEvent(std::string_view playerId,
                                   PlayerEvent event,
                                   std::string value) = 0;

    virtual ~IPlayerObserver() = default;
};

using IPlayerObserverPtr = std::weak_ptr<IPlayerObserver>;
using IPlayerObserverRefPtr = std::shared_ptr<IPlayerObserver>;

} // namespace slark
namespace slark
