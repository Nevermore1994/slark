//
// Created by Nevermore on 2023/7/19.
// slark DecoderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include "DecoderManager.h"

namespace slark {

class DecoderComponent : public NonCopyable {
    using DecoderCallback = std::function<void(AVFrameList)>;
public:
    explicit DecoderComponent(const std::string& name, std::unique_ptr<IDecoder> decoder, DecoderCallback&& callback);
    ~DecoderComponent() override = default;

    void receive(AVFrameList&& packets) noexcept;
    void pause() const noexcept;
    void resume() const noexcept;
private:
    void decode();
private:
    std::unique_ptr<IDecoder> decoder_;
    DecoderCallback callback_;
    Thread decodeWorker_;
    AVFrameSafeDeque packets_;
};

}