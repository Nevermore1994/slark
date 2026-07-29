//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "IEGLContext.h"
#include "PerfTypes.h"

namespace slark {

enum class PlayerState : uint8_t {
    NotInited = 0,
    Initializing,
    Prepared,
    Buffering,
    Ready,
    Playing,
    Pause,
    Stop,
    Error,
    Completed,
    Unknown,
};

enum class PlayerEvent : uint8_t {
    FirstFrameRendered,
    SeekDone,
    PlayEnd,
    UpdateCacheTime,
    OnError,
};

struct ResourceItem {
    std::string path;
    double displayStart = 0;
    double displayDuration = 0;
};

struct PlayerSetting {
    bool isLoop = false;
    bool isMute = false;
    bool enableAudioSoftDecode = false;
    bool enableVideoSoftDecode = false;
    float volume = 100.0f;
    double maxCacheTime = 30.0;
    double minCacheTime = 5.0;
    perf::PerfConfig perfConfig;
    int32_t playerThreadTickMs = 10;
    int32_t playedTimeTickMs = 200;
    int32_t cacheTickMs = 500;
};

struct PlayerParams {
    ResourceItem item;
    PlayerSetting setting;
    std::shared_ptr<IEGLContext> mainGLContext;
};

struct PlayerInfo {
    bool isValid = false;
    bool hasVideo = false;
    bool hasAudio = false;
    double duration = 0;
};

struct IVideoRender;

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

}  // namespace slark
