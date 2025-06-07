//
// Created by Nevermore on 2025/2/25.
//

#pragma once

#include <utility>
#include "AudioInfo.h"
#include "Manager.hpp"

namespace slark {

class AudioRender : public IAudioRender {
public:
    explicit AudioRender(std::shared_ptr<AudioInfo> audioInfo);

    ~AudioRender() override;

public:
    void play() noexcept override;

    void pause() noexcept override;

    void stop() noexcept override;

    void setVolume(float volume) noexcept override;

    void flush() noexcept override;

    void reset() noexcept override;

    Time::TimePoint playedTime() noexcept override;

    const std::string& playerId() const noexcept {
        return playerId_;
    }
private:
    void init() noexcept;

    void release() noexcept;

private:
    std::string playerId_;
    TimerId timerId_{};
    Time::TimePoint latency_;
};

using AudioRenderManager = Manager<AudioRender>;

} // slark

