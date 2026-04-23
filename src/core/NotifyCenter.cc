//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "NotifyCenter.h"

#include "Base.h"
#include "Log.hpp"
#include "Util.h"

namespace slark {

NotifyCenter::NotifyCenter(std::string_view playerId) noexcept : playerId_(playerId) {}

void NotifyCenter::addObserver(IPlayerObserverPtr observer) noexcept {
    observer_.withWriteLock([observer = std::move(observer)](auto& ob) {
        ob = observer;
    });
}

void NotifyCenter::removeObserver() noexcept {
    observer_.withWriteLock([](auto& ob) {
        ob.reset();
    });
}

void NotifyCenter::notifyPlayerState(PlayerState state) noexcept {
    observer_.withReadLock([this, state](auto& observer) {
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyPlayerState(playerId_, state);
        }
    });
}

void NotifyCenter::notifyPlayerEvent(PlayerEvent event, std::string value) noexcept {
    observer_.withReadLock([this, event, value = std::move(value)](auto& observer) {
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyPlayerEvent(playerId_, event, value);
        }
    });
}

void NotifyCenter::notifyPlayedTime(double playedTime,
                                    double duration,
                                    double audioTime,
                                    double videoTime,
                                    bool hasAudio,
                                    bool hasVideo,
                                    bool isEndTime) noexcept {
    double time = 0.0;
    if (!isEndTime) {
        if (hasAudio && hasVideo) {
            time = std::min(videoTime, audioTime);
            time = std::min(time, duration);
            LogI("notifyTime:{}, video time:{}, audio time:{}", time, videoTime, audioTime);
        } else if (hasAudio) {
            time = std::min(audioTime, duration);
            LogI("notifyTime:{} (audio)", time);
        } else if (hasVideo) {
            time = std::min(videoTime, duration);
            LogI("notifyTime:{} (video)", time);
        }
        if (lastNotifyPlayedTime_ > 0 && isEqual(lastNotifyPlayedTime_, time, 0.1)) {
            return;
        }
    } else {
        time = duration;
    }
    lastNotifyPlayedTime_ = time;
    observer_.withReadLock([this, time](auto& observer) {
        if (auto ptr = observer.lock(); ptr) {
            ptr->notifyPlayedTime(playerId_, time);
        }
    });
}

void NotifyCenter::notifyCacheTime(double cachedDuration) noexcept {
    if (isEqual(lastNotifyCacheTime_, cachedDuration, 0.1)) {
        return;
    }
    lastNotifyCacheTime_ = cachedDuration;
    notifyPlayerEvent(PlayerEvent::UpdateCacheTime, std::to_string(cachedDuration));
    LogI("notify cache time:{}", cachedDuration);
}

void NotifyCenter::resetNotifyState() noexcept {
    lastNotifyPlayedTime_ = 0;
    lastNotifyCacheTime_ = 0;
}

} // namespace slark
