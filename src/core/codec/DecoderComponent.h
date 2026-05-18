//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once
#include "Channel.hpp"
#include "DecoderManager.h"
#include "Error.h"
#include "IPipelineComponent.h"
#include "Thread.h"
#include "ThreadManager.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace slark {

struct DecoderComponentConfig {
    DecoderReceiveFunc onData;
    std::function<void()> onReady;
    std::function<void(Error)> onError;
    bool isAudio = false;
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

private:
    void runLoop() noexcept;

private:
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> flushed_{false};
    bool paused_ = false;
    std::mutex pauseMutex_;
    std::condition_variable pauseCv_;

    DecoderComponentConfig config_;
    std::shared_ptr<IDecoder> decoder_;
    std::shared_ptr<Thread> decodeWorker_;
    ReceiverPtr<AVFrameRefPtr> receiver_;
};

} // namespace slark
