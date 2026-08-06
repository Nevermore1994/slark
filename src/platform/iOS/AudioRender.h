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

    /// Return the AudioUnit pipeline latency (MultiChannelMixer + RemoteIO combined)
    [[nodiscard]] Time::TimeDelta getHardwareLatency() noexcept override;

 public:
    bool isNeedRequestData() const noexcept;

 private:
    bool checkFormat() const noexcept;

    bool setupAudioComponent() noexcept;

 private:
    AudioUnit volumeUnit_ = nullptr;
    AudioUnit renderUnit_ = nullptr;
    std::atomic<int64_t> latencyUs_{0};  // in µs; written by the timer, read by getHardwareLatency
    TimerId timerId_ = TimerId::kInvalidTimerId;
};

}  // namespace slark
