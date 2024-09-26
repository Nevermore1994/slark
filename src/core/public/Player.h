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
    OnError,
};

enum class PlayerErrorCode : uint8_t {
    OpenFileError,
};

struct IPlayerObserver {
    virtual void notifyTime(std::string_view playerId, long double time) = 0;

    virtual void notifyState(std::string_view playerId, PlayerState state) = 0;

    virtual void notifyEvent(std::string_view playerId, PlayerEvent event, std::string value) = 0;

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

struct RenderSize {
    uint32_t width = 1280;
    uint32_t height = 720;
    RenderSize() = default;
    RenderSize(uint32_t width, uint32_t height)
        : width(width)
        , height(height) {

    }
};

struct PlayerSetting {
    bool isLoop = false;
    bool enableAudioSoftDecode = false;
    bool enableVideoSoftDecode = false;
    bool isMute = false;
    RenderSize size;
    float volume = 100.0f;
};

struct PlayerParams {
    ResourceItem item;
    PlayerSetting setting;
};

struct PlayerInfo {
    bool hasVideo = false;
    bool hasAudio = false;
    long double duration;
};

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
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

}//end namespace slark
