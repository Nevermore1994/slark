//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <shared_mutex>
#include <string>

#include "Player.h"
#include "Synchronized.hpp"

namespace slark {

class NotifyCenter {
 public:
    explicit NotifyCenter(std::string_view playerId) noexcept;

    void addObserver(IPlayerObserverPtr observer) noexcept;
    void removeObserver() noexcept;

    void notifyPlayerState(PlayerState state) noexcept;
    void notifyPlayerEvent(PlayerEvent event, std::string value = "") noexcept;
    void notifyPlayedTime(double duration,
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
    // 以下两个字段仅在 ownerThread 的 repeat task 中访问，依赖单线程串行语义，无需 atomic
    double lastNotifyPlayedTime_ = 0;
    double lastNotifyCacheTime_ = 0;

    [[nodiscard]] static double resolveCurrentTime(double duration,
                                                   double audioTime,
                                                   double videoTime,
                                                   bool hasAudio,
                                                   bool hasVideo) noexcept;
};

}  // namespace slark
