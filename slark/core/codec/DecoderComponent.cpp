//
// Created by Nevermore on 2023/7/19.
// slark DecoderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "DecoderComponent.h"

namespace slark {

DecoderComponent::DecoderComponent(const std::string& name, std::unique_ptr<IDecoder> decoder, DecoderComponent::DecoderCallback&& callback)
    : decoder_(std::move(decoder))
    , callback_(std::move(callback))
    , decodeWorker_(name, &DecoderComponent::decode, this) {

}

void DecoderComponent::decode() {

}

void DecoderComponent::receive(AVFrameList &&packets) noexcept {

}

void DecoderComponent::pause() const noexcept {

}

void DecoderComponent::resume() const noexcept {

}

}
