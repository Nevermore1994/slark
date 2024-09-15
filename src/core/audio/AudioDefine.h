//
//  AudioDefine.h
//  slark
//
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include <_types/_uint64_t.h>
#include <functional>
#include <utility>
#include "AVFrame.hpp"
#include "Time.hpp"

namespace slark::Audio {

enum class AudioRenderStatus {
    Unknown,
    Ready,
    Play,
    Pause,
    Stop,
    Error,
};

struct AudioInfo {
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint64_t sampleRate = 0;
    uint64_t headerLength = 0;
    slark::CTime duration;
    std::string_view mediaInfo;

    uint64_t bitrate() const {
        return sampleRate * channels * bitsPerSample;
    }
};


class IAudioRender {
public:
    explicit IAudioRender(std::shared_ptr<AudioInfo> audioInfo)
        : audioInfo_(std::move(audioInfo)) {
        
    }
    virtual ~IAudioRender() = default;
    
    virtual void play() noexcept = 0;
    virtual void pause() noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual void setVolume(float volume) noexcept = 0;
    virtual void flush() noexcept = 0;
    
    [[nodiscard]] virtual bool isNeedRequestData() const noexcept = 0;
    
    [[nodiscard]] inline AudioRenderStatus status() const noexcept {
        return status_;
    }
    
    inline const std::shared_ptr<AudioInfo> info() const noexcept {
        SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
        return audioInfo_;
    }
    
    [[nodiscard]] inline float volume() const noexcept {
        return volume_;
    }
    
    std::function<std::unique_ptr<slark::Data>(uint32_t)> requestAudioData;
protected:
    float volume_ = 50.f; // 0 ~ 100
    std::shared_ptr<AudioInfo> audioInfo_ = nullptr;
    AudioRenderStatus status_ = AudioRenderStatus::Unknown;
};

#if (SLARK_IOS || SLARK_ANDROID)
std::unique_ptr<IAudioRender> createAudioRender(std::shared_ptr<AudioInfo> audioInfo);
#else
inline std::unique_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& /*audioInfo*/) {
    return nullptr;
}
#endif
}
