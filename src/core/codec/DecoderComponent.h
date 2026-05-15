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

namespace slark {

using DataProviderResult = std::pair<AVFrameRefPtr, Error>;

struct DecoderComponentConfig {
    DecoderReceiveFunc onData;
    std::function<void()> onReady;
    std::function<void(Error)> onError;
    std::function<DataProviderResult()> dataProvider;
};

class DecoderComponent : public IPipelineComponent,
                         public std::enable_shared_from_this<DecoderComponent> {
public:
    explicit DecoderComponent(DecoderComponentConfig config);

    ~DecoderComponent() override;

    void open(DecoderType type, const std::shared_ptr<DecoderConfig>& config) noexcept;

    void close() noexcept override;

    void flush() noexcept override;

    void start() noexcept override;

    void pause() noexcept override;

    void scheduleDecoding() noexcept;

    [[nodiscard]] bool isRunning() const noexcept override;

private:
    void pushFrameDecode();

private:
    std::atomic_bool isOpened_ = false;
    DecoderComponentConfig config_;
    Synchronized<std::shared_ptr<IDecoder>> decoder_;
    std::shared_ptr<Thread> decodeWorker_;
};

} // namespace slark
