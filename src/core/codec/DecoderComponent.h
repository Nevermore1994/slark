//
// Created by Nevermore on 2023/7/19.
// slark DecoderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include "DecoderManager.h"
#include "Error.h"
#include "IPipelineComponent.h"
#include "Synchronized.hpp"
#include "Thread.h"

#include <functional>
#include <string>

namespace slark {

struct DecoderOpenResult {
    bool success = false;
    Error error = Error::Success;
    std::string message;
};

using DecoderOpenCallback = std::function<void(DecoderOpenResult)>;
using ErrorCallback = std::function<void(Error, bool /*isVideo*/)>;

class DecoderComponent : public IPipelineComponent,
                         public DecoderDataProvider,
                         public std::enable_shared_from_this<DecoderComponent> {
public:
    explicit DecoderComponent(DecoderReceiveFunc&& callback);

    ~DecoderComponent() override;

    bool open(DecoderType type,
              const std::shared_ptr<DecoderConfig>& config,
              DecoderOpenCallback onOpened = nullptr) noexcept;

    void close() noexcept override;

    void send(AVFramePtr packet) noexcept;

    void flush() noexcept override;

    void seekStream() noexcept override {}

    void start() noexcept override;

    void pause() noexcept override;

    void setErrorCallback(ErrorCallback cb) noexcept;

    [[nodiscard]] bool empty() const noexcept;

    /// Returns the max pts (seconds) of the pending decode queue, or -1.0 if empty.
    [[nodiscard]] double pendingMaxPts() const noexcept;

    [[nodiscard]] bool isRunning() const noexcept override;

    [[nodiscard]] bool isInputCompleted() const noexcept;

    void setInputCompleted() noexcept;

    [[nodiscard]] bool isDecodeCompleted() const noexcept;

    AVFrameRefPtr requestDecodeFrame(bool isBlocking) noexcept override;

private:
    void pushFrameDecode();

    AVFrameRefPtr peekDecodeFrame() noexcept;

    static AVFrameRefPtr buildEOSFrame(bool isVideo) noexcept;

private:
    static constexpr int32_t kMaxConsecutiveFailCount = 5;

    bool isVideo_ = false;
    std::atomic_bool isOpened_ = false;
    std::atomic_bool isInputCompleted_ = false;
    std::atomic_bool eosInjected_ = false;
    std::atomic<int32_t> consecutiveFailCount_ = 0;
    DecoderReceiveFunc callback_;
    ErrorCallback errorCallback_;
    Synchronized<std::shared_ptr<IDecoder>> decoder_;
    std::shared_ptr<Thread> decodeWorker_;
    mutable std::mutex mutex_;
    std::deque<AVFrameRefPtr> pendingDecodeQueue_;
    std::condition_variable cond_;
};

} // namespace slark
