//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "IEGLContext.h"
#include "PerfTypes.h"
#include "FlowConfig.h"

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

    [[nodiscard]] bool hasDisplayRange() const noexcept {
        return displayStart > 0.0 || displayDuration > 0.0;
    }
};

struct PlayerSetting {
    bool isLoop = false;
    bool enableAudioSoftDecode = false;
    bool enableVideoSoftDecode = false;
    float volume = 100.0f;
    perf::PerfConfig perfConfig;

    std::optional<FlowConfig> flowConfig;
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
    bool isSeekable = true;
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
