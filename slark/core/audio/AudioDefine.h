//
//  AudioDefine.h
//  slark
//
//  Created by Nevermore on 2022/8/19.
//
#pragma once
#include "AVFrame.hpp"
#include "Assert.hpp"
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
    slark::CTime offset;
    slark::CTime duration;
    std::string_view mediaInfo;
};


class IAudioRender {
public:
    explicit IAudioRender(std::shared_ptr<AudioInfo> audioInfo)
        : audioInfo_(audioInfo) {
        
    }
    virtual ~IAudioRender() = default;
    
    virtual void play() noexcept = 0;
    virtual void pause() noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual void setVolume(float volume) noexcept = 0;
    virtual void flush() noexcept = 0;
    
    virtual bool isNeedRequestData() const noexcept = 0;
    
    inline AudioRenderStatus status() const noexcept {
        return status_;
    }
    
    const std::shared_ptr<AudioInfo> info() const noexcept {
        SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
        return audioInfo_;
    }
    
    std::function<std::unique_ptr<slark::Data>(uint32_t)> requestNextAudioData;
protected:
    std::shared_ptr<AudioInfo> audioInfo_ = nullptr;
    AudioRenderStatus status_ = AudioRenderStatus::Unknown;
};

#if (SLARK_IOS || SLRAK_ANDROID)
std::unique_ptr<IAudioRender> createAudioRender(std::shared_ptr<AudioInfo> audioInfo);
#else
inline std::unique_ptr<IAudioRender> createAudioRender(std::shared_ptr<AudioInfo> /*audioInfo*/) {
    return nullptr;
}
#endif
}
