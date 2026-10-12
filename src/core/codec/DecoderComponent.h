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
#include "FlowConfig.h"
#include "LoopThread.h"
#include "ThreadManager.h"
#include "pipeline/WatermarkGate.h"

namespace slark {

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
    void open(std::shared_ptr<IDecoder> decoder,
              const std::shared_ptr<DecoderConfig>& config) noexcept;

    void close() noexcept override;

    void flush() noexcept override;

    void start() noexcept override;

    void pause() noexcept override;

    void resume() noexcept;

    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept override;

    [[nodiscard]] std::shared_ptr<IDecoder> decoder() const noexcept {
        return decoder_.load();
    }

    // Packets queued for the decoder but not consumed yet (the Q2 backlog).
    [[nodiscard]] uint32_t pendingCount() const noexcept {
        return receiver_ ? receiver_->size() : 0;
    }

 private:
    void runLoop(LoopThread& thread) noexcept;

    void drainRenderedOutputs(LoopThread& thread) noexcept;

    [[nodiscard]] AVFrameRefPtr waitForFrame(LoopThread& thread) noexcept;

    void waitForPauseResume(LoopThread& thread) noexcept;

    void evaluateFeedingPauseState() noexcept;

    [[nodiscard]] bool isFeedingPaused() const noexcept;

    [[nodiscard]] bool decodeFrame(AVFrameRefPtr& frame, LoopThread& thread) noexcept;

 private:
    static constexpr int kMaxDecodeRetry = 100;
    static constexpr auto kMaxDecodeBackoff = std::chrono::milliseconds(16);
    static constexpr auto kPausedPollInterval = std::chrono::milliseconds(2);

 private:
    std::atomic<bool> flushed_{false};

    DecoderComponentConfig config_;
    AtomicSharedPtr<IDecoder> decoder_;
    AtomicSharedPtr<LoopThread> decodeWorker_;
    std::mutex workerMutex_;
    std::once_flag finalizeOnce_;
    std::atomic<bool> stopped_{false};
    ReceiverPtr<AVFrameRefPtr> receiver_;

    // Sampled by the decode worker, forced by the owner thread on seek / stop.
    WatermarkGate<int32_t> gate_;
};

}  // namespace slark
