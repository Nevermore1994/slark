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
#include "Synchronized.hpp"
#include "Clock.h"

namespace slark {

enum class AudioProfile : uint8_t {
    AAC_MAIN = 1,
    AAC_LC = 2,
    AAC_SSR = 3,
    AAC_LTP = 4,
    AAC_HE = 5,
    AAC_HE_SP = 29,
};

struct AudioInfo {
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint64_t sampleRate = 0;
    uint32_t timeScale = 0;
    uint32_t samplingFrequencyIndex = 4;
    uint32_t samplingFreqIndexExt{};
    uint8_t audioObjectTypeExt{};
    AudioProfile profile = AudioProfile::AAC_LC;
    std::string mediaInfo;

    [[nodiscard]] uint64_t bitrate() const {
        return sampleRate * channels * bitsPerSample;
    }

    [[nodiscard]] uint64_t bytePerSecond() const {
        return sampleRate * channels * bitsPerSample / 8;
    }

    [[nodiscard]] uint64_t bytePerSample() const {
        return channels * bitsPerSample / 8;
    }

    [[nodiscard]] Time::TimePoint dataLen2TimePoint(uint64_t dataLen) const {
        auto t = static_cast<uint64_t>(static_cast<double>(dataLen) / static_cast<double>(bytePerSample() * sampleRate) * 1000000);
        return t;
    }

    [[nodiscard]] uint64_t timePoint2DataLen(Time::TimePoint point) const {
        return static_cast<uint64_t>(point.second() * static_cast<double>(bytePerSecond()));
    }

    [[nodiscard]] std::shared_ptr<AudioInfo> copy() const {
        auto ptr = std::make_shared<AudioInfo>();
        *ptr = *this;
        return ptr;
    }
};

enum class AudioDataFlag {
    Normal,
    EndOfStream,
    Silence,
    Error,
};

class IAudioRender;

using RequestAudioDataFunc = std::function<uint32_t(uint8_t*, uint32_t, AudioDataFlag&)>;

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

    virtual void reset() noexcept = 0;

    virtual Time::TimePoint playedTime() noexcept = 0;

    [[nodiscard]] inline RenderStatus status() const noexcept {
        return status_;
    }
    
    [[nodiscard]] bool isNormal() const noexcept {
        return status_ == RenderStatus::Error;
    }

    [[nodiscard]] const std::shared_ptr<AudioInfo>& info() const noexcept {
        SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
        return audioInfo_;
    }
    
    [[nodiscard]] inline float volume() const noexcept {
        return volume_;
    }

    void setProvider(RequestAudioDataFunc&& func) noexcept {
        dataFunc_.reset(std::make_shared<RequestAudioDataFunc>(func));
    }

    void seek(double time) noexcept {
        flush();
        renderedDataLength_ = 0;
        clock_.setTime(Time::TimePoint(time));
    }

    void setRenderedLength(uint64_t length) noexcept {
        renderedDataLength_.store(length);
    }

    uint32_t requestAudioData(uint8_t* data, uint32_t size, AudioDataFlag& flag) noexcept {
        if (auto func = dataFunc_.load()) {
            auto getSize = std::invoke(*func, data, size, flag);
            if (getSize != 0) {
                renderedDataLength_ += getSize;
                clock_.setTime(audioInfo_->dataLen2TimePoint(renderedDataLength_));
            }
            return getSize;
        } else {
            flag = AudioDataFlag::Error;
            LogE("request data func is null");
        }
        return 0;
    }
protected:
    float volume_ = 100.f; // 0 ~ 100
    std::shared_ptr<AudioInfo> audioInfo_ = nullptr;
    std::atomic<uint64_t> renderedDataLength_ = 0;
    Clock clock_;
    RenderStatus status_ = RenderStatus::Unknown;
    AtomicSharedPtr<RequestAudioDataFunc> dataFunc_;
};

#if (SLARK_IOS || SLARK_ANDROID)
std::shared_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& audioInfo);
#else
inline std::shared_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& /*audioInfo*/) {
    return nullptr;
}
#endif
}
