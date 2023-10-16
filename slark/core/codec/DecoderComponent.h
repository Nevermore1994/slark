//
// Created by Nevermore on 2023/7/19.
// slark DecoderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include "DecoderManager.h"

namespace slark {

class DecoderComponent : public NonCopyable {
public:
    explicit DecoderComponent(const std::string& name, DecoderReceiveCallback&& callback);
    ~DecoderComponent() override = default;

    void setDecoder(std::unique_ptr<IDecoder> decoder) noexcept;
    void receive(AVFrameArray&& packets) noexcept;
    void pause() noexcept;
    void resume() noexcept;
    void reset() noexcept;

    inline IDecoder& decoder() noexcept {
        return *decoder_;
    }

    inline void setReachFileEnd() noexcept {
        isReachEnd_ = true;
    }

private:
    void decode();
private:
    std::atomic<bool> isReachEnd_ = false;
    std::unique_ptr<IDecoder> decoder_;
    DecoderReceiveCallback callback_;
    Thread decodeWorker_;
    AVFramePtrSafeDeque packets_;
};

}
