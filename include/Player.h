//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "PlayerTypes.h"

namespace slark {

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
