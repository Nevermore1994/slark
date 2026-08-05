//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <AudioToolbox/AudioToolbox.h>

#include <atomic>

#include "AudioDescription.h"
#include "AudioInfo.h"
#include "Data.h"
#include "Timer.h"

namespace slark {

class AudioRender : public IAudioRender {
 public:
    explicit AudioRender(std::shared_ptr<AudioInfo> audioInfo);

    ~AudioRender() override;

    void play() noexcept override;

    void pause() noexcept override;

    void stop() noexcept override;

    void reset() noexcept override;

    void setVolume(float volume) noexcept override;

    void flush() noexcept override;

    void renderEnd() noexcept override;

    /// 返回 AudioUnit pipeline 延迟（MultiChannelMixer + RemoteIO 之和）
    [[nodiscard]] Time::TimeDelta getHardwareLatency() noexcept override;

 public:
    bool isNeedRequestData() const noexcept;

 private:
    bool checkFormat() const noexcept;

    bool setupAudioComponent() noexcept;

 private:
    AudioUnit volumeUnit_ = nullptr;
    AudioUnit renderUnit_ = nullptr;
    std::atomic<int64_t> latencyUs_{0};  // 单位 µs，定时器写，getHardwareLatency 读
    TimerId timerId_ = TimerId::kInvalidTimerId;
};

}  // namespace slark
