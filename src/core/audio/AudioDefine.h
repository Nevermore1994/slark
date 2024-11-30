//
//  AudioDefine.h
//  slark
//
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include <functional>
#include <utility>
#include "MediaDefs.h"
#include "AVFrame.hpp"
#include "Time.hpp"

namespace slark {

struct AudioInfo {
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint64_t sampleRate = 0;
    uint32_t timeScale = 0;
    std::string_view mediaInfo;

    uint64_t bitrate() const {
        return sampleRate * channels * bitsPerSample;
    }
    
    uint64_t bytePerSecond() const {
        return sampleRate * channels * bitsPerSample / 8;
    }
    
    uint64_t bytePerSample() const {
        return channels * bitsPerSample / 8;
    }
    
    Time::TimePoint dataLen2TimePoint(uint64_t dataLen) const {
        auto t = static_cast<uint64_t>(static_cast<long double>(dataLen) / static_cast<long double>(bytePerSample() * sampleRate) * 1000000);
        return t;
    }
     
    uint64_t timePoint2DataLen(Time::TimePoint point) const {
        return static_cast<uint64_t>(point.second() * bytePerSecond());
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
    virtual Time::TimePoint latency() noexcept = 0;
    
    [[nodiscard]] virtual bool isNeedRequestData() const noexcept = 0;
    
    [[nodiscard]] inline RenderStatus status() const noexcept {
        return status_;
    }
    
    [[nodiscard]] inline bool isErrorState() const noexcept {
        return status_ == RenderStatus::Error;
    }
    
    inline const std::shared_ptr<AudioInfo>& info() const noexcept {
        SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
        return audioInfo_;
    }
    
    [[nodiscard]] inline float volume() const noexcept {
        return volume_;
    }
    
    std::function<uint32_t(uint8_t*, uint32_t)> requestAudioData;
protected:
    float volume_ = 100.f; // 0 ~ 100
    std::shared_ptr<AudioInfo> audioInfo_ = nullptr;
    RenderStatus status_ = RenderStatus::Unknown;
};

#if (SLARK_IOS || SLARK_ANDROID)
std::unique_ptr<IAudioRender> createAudioRender(std::shared_ptr<AudioInfo> audioInfo);
#else
inline std::unique_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& /*audioInfo*/) {
    return nullptr;
}
#endif
}
