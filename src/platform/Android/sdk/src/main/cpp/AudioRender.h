//
// Created by Nevermore on 2025/2/25.
//

#pragma once

#include "AudioInfo.h"
#include "Manager.hpp"

#include <utility>

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

    void renderEnd() noexcept override;

    [[nodiscard]] Time::TimeDelta getHardwareLatency() noexcept override;

    const std::string& playerId() const noexcept {
        return playerId_;
    }

private:
    void init() noexcept;

    void release() noexcept;

private:
    std::string playerId_;
};

using AudioRenderManager = Manager<AudioRender>;

} // namespace slark
