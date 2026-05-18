// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "AudioInfo.h"

#include <aaudio/AAudio.h>
#include <atomic>

namespace slark {

class AAudioRender : public IAudioRender {
public:
    explicit AAudioRender(std::shared_ptr<AudioInfo> audioInfo);
    ~AAudioRender() override;

    void play() noexcept override;
    void pause() noexcept override;
    void stop() noexcept override;
    void setVolume(float volume) noexcept override;
    void flush() noexcept override;
    void reset() noexcept override;
    void renderEnd() noexcept override;

    /// 返回 AAudio stream 的硬件 pipeline 延迟
    [[nodiscard]] Time::TimeDelta getHardwareLatency() noexcept override;

private:
    static aaudio_data_callback_result_t dataCallback(AAudioStream* stream,
                                                      void* userData,
                                                      void* audioData,
                                                      int32_t numFrames);

    void initStream() noexcept;
    void releaseStream() noexcept;

private:
    AAudioStream* stream_ = nullptr;
    AAudioStreamBuilder* builder_ = nullptr;
};

} // namespace slark
