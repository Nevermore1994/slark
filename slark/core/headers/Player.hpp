//
//  Player.hpp
//  Slark
//
//  Created by Nevermore on 2022/4/22.
//

#pragma once

#include "Time.hpp"

#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <memory>

namespace Slark {

enum class PlayerState {
    Unknown = 0,
    Initializing,
    Ready,
    Buffering,
    Playing,
    Pause,
    Stop,
};

enum class PlayerEvent {
    FirstFrameRendered,
    SeekDone,
    PlayEnd,
};

struct IPlayerObserver {
    virtual void updateTime(long double time) = 0;

    virtual void updateState(PlayerState state) = 0;

    virtual void event(PlayerEvent event) = 0;

    virtual ~IPlayerObserver() = default;
};

struct ResourceItem {
    std::string path;
    CTimeRange displayTimeRange;
};

struct PlayerParams {
    bool isLoop = false;
    std::vector<ResourceItem> items;
    std::weak_ptr<IPlayerObserver> observer;
};

struct PlayerInfos {
    CTime duration = CTime();
};

class Player {

public:
    Player(std::shared_ptr<PlayerParams> params);

    ~Player();

public:
    void play() noexcept;

    void stop() noexcept;

    void pause() noexcept;

    void resume() noexcept;

    void seek(long double time) noexcept;

    void seek(long double time, bool isAccurate) noexcept;

    std::string_view playerId() const noexcept;

public:
    const PlayerParams& observer();

    PlayerState state();

    const PlayerInfos& info();

private:
    class Impl;

    std::unique_ptr<Impl> pimpl_;
};

}//end namespace Slark
