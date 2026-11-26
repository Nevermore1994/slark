//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <string>

#include "PlayerTypes.h"

namespace slark {

class Player {
 public:
    explicit Player(std::unique_ptr<PlayerParams> params);

    ~Player();

    static void preResolve(const DnsConfig& config);

 public:
    void prepare() noexcept;

    void play() noexcept;

    // Asynchronous and idempotent; completion is Snapshot.status == Idle via the observer.
    void stop() noexcept;

    void pause() noexcept;

    void seek(double time, bool isAccurate = false) noexcept;

    void setLoop(bool isLoop);

    void setVolume(float volume);

    void setMute(bool isMute);

    void addObserver(IPlayerObserverPtr observer) noexcept;

    void removeObserver() noexcept;

    void setRenderImpl(std::weak_ptr<IVideoRender> render) noexcept;

    // Platform render loops query this instead of maintaining their own state whitelist.
    void setRenderSuspended(bool suspended) noexcept;

 public:
    PlayerParams peek() noexcept;

    PlayerState state() noexcept;

    [[nodiscard]] bool vsyncDesired() noexcept;

    PlayerInfo info() noexcept;

    [[nodiscard]] std::string_view playerId() const noexcept;

    [[nodiscard]] double currentPlayedTime() noexcept;

    // PotPlayer-style diagnostics; the backend line is appended by the platform layer.
    [[nodiscard]] std::string debugSnapshot() noexcept;

 private:
    class Impl;
    std::shared_ptr<Impl> pimpl_;
};

}  // namespace slark
