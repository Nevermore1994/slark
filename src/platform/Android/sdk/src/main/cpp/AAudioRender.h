//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "AudioInfo.h"

#include <aaudio/AAudio.h>
#include <atomic>
#include <memory>

namespace slark {

class AAudioRender : public IAudioRender, public std::enable_shared_from_this<AAudioRender> {
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

    [[nodiscard]] Time::TimeDelta getHardwareLatency() noexcept override;

private:
    static aaudio_data_callback_result_t dataCallback(AAudioStream* stream,
                                                      void* userData,
                                                      void* audioData,
                                                      int32_t numFrames);

    static void errorCallback(AAudioStream* stream, void* userData, aaudio_result_t error) noexcept;

    void initStream() noexcept;
    void releaseStream() noexcept;

private:
    AAudioStream* stream_ = nullptr;
    AAudioStreamBuilder* builder_ = nullptr;
    std::atomic<int64_t> flushBaseFrames_{0};
};

} // namespace slark
