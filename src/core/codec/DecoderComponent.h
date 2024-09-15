//
// Created by Nevermore on 2023/7/19.
// slark DecoderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include "DecoderManager.h"
#include "Thread.h"

namespace slark {

class DecoderComponent : public NonCopyable {
public:
    explicit DecoderComponent(DecoderType decodeType, DecoderReceiveFunc&& callback);
    ~DecoderComponent() override = default;

    void send(AVFrameArray&& packets) noexcept;
    void send(AVFramePtr packet) noexcept;
    void pause() noexcept;
    void resume() noexcept;
    void reset() noexcept;
    void close() noexcept;

    const std::unique_ptr<IDecoder>& decoder() noexcept {
        return decoder_;
    }

    void setReachEnd(bool isReachEnd) noexcept {
        isReachEnd_ = isReachEnd;
    }

private:
    void decode();
private:
    std::atomic<bool> isReachEnd_ = false;
    std::unique_ptr<IDecoder> decoder_;
    DecoderReceiveFunc callback_;
    Thread decodeWorker_;
    Synchronized<std::vector<AVFramePtr>> packets_;
};

}
