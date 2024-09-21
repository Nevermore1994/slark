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

enum class PlayerState : int8_t {
    Unknown = 0,
    Initializing,
    Ready,
    Buffering,
    Playing,
    Pause,
    Stop,
    Error,
    Completed,
};

enum class PlayerEvent : int8_t {
    FirstFrameRendered,
    SeekDone,
    PlayEnd,
    OnError,
};

struct IPlayerObserver {
    virtual void notifyTime(std::string_view playerId, long double time) = 0;

    virtual void notifyState(std::string_view playerId, PlayerState state) = 0;

    virtual void event(std::string_view playerId, PlayerEvent event, std::string value) = 0;

    virtual ~IPlayerObserver() = default;
};

using IPlayerObserverPtr = std::weak_ptr<IPlayerObserver>;
using IPlayerObserverPtrArray = std::vector<IPlayerObserverPtr>;

struct ResourceItem {
    ///play media path, local path or http url
    std::string path;
    ///play start offset, second
    double displayStart;
    ///play duration, second
    double displayDuration;
};

struct RenderSize {
    uint32_t width = 720;
    uint32_t height = 1280;
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
    RenderSize size;
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

    [[nodiscard]] std::string_view playerId() const noexcept;

    void updateSetting(PlayerSetting setting) noexcept;

    void addObserver(IPlayerObserverPtr observer) noexcept;
    
    void addObserver(IPlayerObserverPtrArray observers) noexcept;
    
    void removeObserver(const IPlayerObserverPtr& observer) noexcept;
    
    void removeObserver(const IPlayerObserverPtrArray& observers) noexcept;

public:
    PlayerParams peek() noexcept;

    PlayerState state() noexcept;
    
    const PlayerInfo& info() noexcept;
private:
    void setState(PlayerState state) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

}//end namespace slark