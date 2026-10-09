//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <aaudio/AAudio.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "AccessGate.hpp"
#include "AudioInfo.h"

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

    [[nodiscard]] Time::TimeDelta getHardwareLatency() noexcept override;

 private:
    static aaudio_data_callback_result_t dataCallback(AAudioStream* stream,
                                                      void* userData,
                                                      void* audioData,
                                                      int32_t numFrames);

    static void errorCallback(AAudioStream* stream, void* userData, aaudio_result_t error) noexcept;

    void recoverIfNeeded() noexcept;

    void initStream() noexcept;
    void releaseStream() noexcept;

 private:
    AAudioStream* stream_ = nullptr;
    AAudioStreamBuilder* builder_ = nullptr;
    std::mutex streamMutex_;
    AccessGate callbackGate_{false};
    std::atomic<bool> closing_{false};
    std::atomic<bool> recoveryRequested_{false};
    std::atomic<aaudio_result_t> recoveryError_{AAUDIO_OK};
    std::atomic<int64_t> flushBaseFrames_{0};
};

}  // namespace slark
