//
// Created by Nevermore on 2025/2/25.
//

#pragma once
#include <utility>
#include "AudioDefine.h"

namespace slark {

class AudioRender : public IAudioRender {
    explicit AudioRender(std::shared_ptr<AudioInfo> audioInfo);

    ~AudioRender() override = default;
public:
    void play() noexcept override;

    void pause() noexcept override;

    void stop() noexcept override;

    void setVolume(float volume) noexcept override;

    void flush() noexcept override;
private:
    void init() noexcept;

private:
    std::string playerId_;
};

} // slark

