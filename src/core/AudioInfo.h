//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once
#include <functional>
#include <utility>

#include "AVFrame.hpp"
#include "Log.hpp"
#include "MediaDefs.h"
#include "Synchronized.hpp"
#include "Time.hpp"

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
        // 整数运算：dataLen * 1_000_000 / (bytePerSample * sampleRate)，避免浮点精度损失
        const uint64_t divisor = bytePerSample() * sampleRate;
        if (divisor == 0) {
            return Time::TimePoint{0};
        }
        return Time::TimePoint{dataLen * 1'000'000ULL / divisor};
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
        : audioInfo_(std::move(audioInfo)) {}

    virtual ~IAudioRender() = default;

    virtual void play() noexcept = 0;
    virtual void pause() noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual void setVolume(float volume) noexcept = 0;
    virtual void flush() noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void renderEnd() noexcept = 0;

    [[nodiscard]] virtual Time::TimeDelta getHardwareLatency() noexcept = 0;

    void setProvider(RequestAudioDataFunc&& func) noexcept {
        dataFunc_.reset(std::make_shared<RequestAudioDataFunc>(std::move(func)));
    }

    uint32_t requestAudioData(uint8_t* buf, uint32_t size, AudioDataFlag& flag) noexcept {
        auto func = dataFunc_.load();
        if (func && *func) {
            return (*func)(buf, size, flag);
        }
        flag = AudioDataFlag::Error;
        return 0;
    }

    [[nodiscard]] RenderStatus status() const noexcept {
        return status_;
    }

    [[nodiscard]] bool isNormal() const noexcept {
        return status_ != RenderStatus::Error;
    }

    [[nodiscard]] float volume() const noexcept {
        return volume_;
    }

    [[nodiscard]] const std::shared_ptr<AudioInfo>& info() const noexcept {
        SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
        return audioInfo_;
    }

 protected:
    std::atomic<float> volume_{1.0f};
    std::shared_ptr<AudioInfo> audioInfo_;
    std::atomic<RenderStatus> status_{RenderStatus::Unknown};
    AtomicSharedPtr<RequestAudioDataFunc> dataFunc_;
};

#if (SLARK_IOS || SLARK_ANDROID)
std::shared_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& audioInfo);
#else
inline std::shared_ptr<IAudioRender> createAudioRender(
    const std::shared_ptr<AudioInfo>& /*audioInfo*/) {
    return nullptr;
}
#endif
}  // namespace slark
