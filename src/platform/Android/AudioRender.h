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

    void sendAudioData(DataPtr audioData) noexcept;

    Time::TimePoint latency() noexcept override;
private:
    void init() noexcept;

private:
    std::string playerId_;
    Time::TimePoint latency_;
};

class AudioRenderManager {
public:
    static AudioRenderManager& shareInstance() noexcept;

    ~AudioRenderManager() = default;
public:
    std::shared_ptr<AudioRender> find(const std::string& decoderId) const noexcept;

    void add(const std::string& decoderId, std::shared_ptr<AudioRender> ptr) noexcept;

    void remove(const std::string& decoderId) noexcept;
private:
    AudioRenderManager() = default;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<AudioRender>> players_;
};


} // slark

