//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Player.h"
#include "Synchronized.hpp"

#include <shared_mutex>
#include <string>

namespace slark {

class NotifyCenter {
public:
    explicit NotifyCenter(std::string_view playerId) noexcept;

    void addObserver(IPlayerObserverPtr observer) noexcept;
    void removeObserver() noexcept;

    void notifyPlayerState(PlayerState state) noexcept;
    void notifyPlayerEvent(PlayerEvent event, std::string value = "") noexcept;
    void notifyPlayedTime(double playedTime,
                          double duration,
                          double audioTime,
                          double videoTime,
                          bool hasAudio,
                          bool hasVideo,
                          bool isEndTime = false) noexcept;
    void notifyCacheTime(double cachedDuration) noexcept;
    void resetNotifyState() noexcept;

private:
    std::string playerId_;
    Synchronized<IPlayerObserverPtr, std::shared_mutex> observer_;
    double lastNotifyPlayedTime_ = 0;
    double lastNotifyCacheTime_ = 0;
};

} // namespace slark
namespace slark
