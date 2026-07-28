//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "IEGLContext.h"
#include "IPlayerObserver.h"
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
    std::string path;            ///< local path or http url
    double displayStart = 0;     ///< play start offset, seconds
    double displayDuration = 0;  ///< play duration, seconds
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

class DemuxerHelper;

class Player {
 public:
    explicit Player(std::unique_ptr<PlayerParams> params);

    ~Player();

 public:
    void prepare() noexcept;

    void play() noexcept;

    void stop() noexcept;

    void pause() noexcept;

    void seek(double time, bool isAccurate = false) noexcept;

    void setLoop(bool isLoop);

    void setVolume(float volume);

    void setMute(bool isMute);

    void addObserver(IPlayerObserverPtr observer) noexcept;

    void removeObserver() noexcept;

    void setRenderImpl(std::weak_ptr<IVideoRender> render) noexcept;

 public:
    PlayerParams peek() noexcept;

    PlayerState state() noexcept;

    PlayerInfo info() noexcept;

    [[nodiscard]] std::string_view playerId() const noexcept;

    [[nodiscard]] double currentPlayedTime() noexcept;

 private:
    class Impl;
    std::shared_ptr<Impl> pimpl_;
};

}  // namespace slark
