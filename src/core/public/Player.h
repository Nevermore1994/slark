//
//  Player.h
//  slark
//
//  Created by Nevermore on 2022/4/22.
//

#pragma once

#include <string_view>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <memory>
#include "IEGLContext.h"

namespace slark {

enum class PlayerState : uint8_t {
    Unknown = 0,
    Initializing,
    Buffering,
    Ready,
    Playing,
    Pause,
    Stop,
    Error,
    Completed,
};

enum class PlayerEvent : uint8_t {
    FirstFrameRendered,
    SeekDone,
    PlayEnd,
    UpdateCacheTime,
    OnError,
};

enum class PlayerErrorCode : uint8_t {
    OpenFileError,
};

struct IPlayerObserver {
    virtual void notifyPlayedTime(std::string_view playerId, long double time) = 0;

    virtual void notifyPlayerState(std::string_view playerId, PlayerState state) = 0;

    virtual void notifyPlayerEvent(std::string_view playerId, PlayerEvent event, std::string value) = 0;
    
    virtual ~IPlayerObserver() = default;
};

using IPlayerObserverPtr = std::weak_ptr<IPlayerObserver>;
using IPlayerObserverRefPtr = std::shared_ptr<IPlayerObserver>;

struct ResourceItem {
    ///play media path, local path or http url
    std::string path;
    ///play start offset, second
    double displayStart;
    ///play duration, second
    double displayDuration;
};

struct PlayerSetting {
    bool isLoop = false;
    bool enableAudioSoftDecode = false;
    bool enableVideoSoftDecode = false;
    bool isMute = false;
    uint32_t width = 0;
    uint32_t height = 0;
    float volume = 100.0f;
    long double maxCacheTime = 30.0; //seconds
    long double minCacheTime = 5.0; //seconds
};

struct PlayerParams {
    ResourceItem item;
    PlayerSetting setting;
    std::shared_ptr<IEGLContext> mainGLContext;
};

struct PlayerInfo {
    bool hasVideo = false;
    bool hasAudio = false;
    long double duration = 0;
};

struct IVideoRender;

class DemuxerHelper;

class Player {

public:
    explicit Player(std::unique_ptr<PlayerParams> params);

    ~Player();

public:
    void play() noexcept;

    void stop() noexcept;

    void pause() noexcept;

    void seek(long double time) noexcept;

    void seek(long double time, bool isAccurate) noexcept;

    void setLoop(bool isLoop);
    
    void setVolume(float volume);
    
    void setMute(bool isMute);
     
    void setRenderSize(uint32_t width, uint32_t height);

    void addObserver(IPlayerObserverPtr observer) noexcept;
    
    void removeObserver() noexcept;

    void* requestRender() noexcept;
    
    void setRenderImpl(std::weak_ptr<IVideoRender>& render);
public:
    PlayerParams peek() noexcept;
    
    PlayerSetting peekSetting() noexcept;

    PlayerState state() noexcept;
    
    const PlayerInfo& info() noexcept;
    
    [[nodiscard]] std::string_view playerId() const noexcept;
    
    [[nodiscard]] long double currentPlayedTime() noexcept;
private:
    void setState(PlayerState state) noexcept;

private:
    friend class PlayerImplHelper;
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

}//end namespace slark
