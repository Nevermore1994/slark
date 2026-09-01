//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

#include "Channel.hpp"
#include "DecoderManager.h"
#include "Error.h"
#include "IDecoder.h"
#include "IPipelineComponent.h"
#include "PipelineConfig.h"
#include "Thread.h"
#include "ThreadManager.h"

namespace slark {

// Hysteresis: a paused decoder resumes below the low watermark, not on the next sample.
[[nodiscard]] inline bool shouldPauseFeeding(int32_t pendingFrames,
                                             int32_t lowWatermark,
                                             int32_t highWatermark,
                                             bool currentlyPaused) noexcept {
    if (highWatermark <= lowWatermark) {
        return currentlyPaused;
    }
    if (currentlyPaused) {
        return pendingFrames > lowWatermark;
    }
    return pendingFrames >= highWatermark;
}

struct DecoderComponentConfig {
    DecoderReceiveFunc onData;
    std::function<void()> onReady;
    std::function<void(Error)> onError;
    bool isAudio = false;
    std::function<int32_t()> pendingFrameCount;
    DecoderBackpressureConfig backpressure;
};

class DecoderComponent : public IPipelineComponent,
                         public std::enable_shared_from_this<DecoderComponent> {
 public:
    explicit DecoderComponent(ReceiverPtr<AVFrameRefPtr> receiver, DecoderComponentConfig config);

    ~DecoderComponent() override;

    void open(DecoderType type, const std::shared_ptr<DecoderConfig>& config) noexcept;

    void close() noexcept override;

    void flush() noexcept override;

    void start() noexcept override;

    void pause() noexcept override;

    void resume() noexcept;

    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept override;

    [[nodiscard]] std::shared_ptr<IDecoder> decoder() const noexcept {
        return decoder_;
    }

 private:
    void runLoop(Thread& thread) noexcept;

    void drainRenderedOutputs(Thread& thread) noexcept;

    [[nodiscard]] AVFrameRefPtr waitForFrame(Thread& thread) noexcept;

    void waitForPauseResume(Thread& thread) noexcept;

    void evaluateFeedingPauseState() noexcept;

    [[nodiscard]] bool isFeedingPaused() const noexcept;

    [[nodiscard]] bool decodeFrame(AVFrameRefPtr& frame, Thread& thread) noexcept;

 private:
    static constexpr int kMaxDecodeRetry = 100;
    static constexpr auto kMaxDecodeBackoff = std::chrono::milliseconds(16);
    static constexpr auto kPausedPollInterval = std::chrono::milliseconds(2);

 private:
    std::atomic<bool> flushed_{false};
    std::atomic<bool> backpressurePaused_{false};
    std::atomic<bool> operationPaused_{false};

    DecoderComponentConfig config_;
    std::shared_ptr<IDecoder> decoder_;
    std::shared_ptr<Thread> decodeWorker_;
    ReceiverPtr<AVFrameRefPtr> receiver_;
};

}  // namespace slark
