//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <aaudio/AAudio.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

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
    class CallbackScope {
     public:
        explicit CallbackScope(AAudioRender* render) noexcept;
        ~CallbackScope();

        explicit operator bool() const noexcept {
            return entered_;
        }

     private:
        AAudioRender* render_ = nullptr;
        bool entered_ = false;
    };

    static aaudio_data_callback_result_t dataCallback(AAudioStream* stream,
                                                      void* userData,
                                                      void* audioData,
                                                      int32_t numFrames);

    static void errorCallback(AAudioStream* stream, void* userData, aaudio_result_t error) noexcept;

    bool tryEnterCallback() noexcept;
    void leaveCallback() noexcept;
    void closeCallbackGateAndWait() noexcept;
    void openCallbackGate() noexcept;
    void recoverIfNeeded() noexcept;

    void initStream() noexcept;
    void releaseStream() noexcept;

 private:
    static constexpr uint32_t kCallbackGateClosed = 1U << 31;
    static constexpr uint32_t kCallbackCountMask = ~kCallbackGateClosed;

    AAudioStream* stream_ = nullptr;
    AAudioStreamBuilder* builder_ = nullptr;
    std::mutex streamMutex_;
    std::atomic<uint32_t> callbackState_{kCallbackGateClosed};
    std::mutex callbackWaitMutex_;
    std::condition_variable callbackWaitCondition_;
    std::atomic<bool> closing_{false};
    std::atomic<bool> recoveryRequested_{false};
    std::atomic<aaudio_result_t> recoveryError_{AAUDIO_OK};
    std::atomic<int64_t> flushBaseFrames_{0};
};

}  // namespace slark
