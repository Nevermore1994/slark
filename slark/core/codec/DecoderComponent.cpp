//
// Created by Nevermore on 2023/7/19.
// slark DecoderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "DecoderComponent.h"
#include "Log.hpp"

namespace slark {

DecoderComponent::DecoderComponent(const std::string& name, DecoderReceiveCallback&& callback)
    : decoder_(nullptr)
    , callback_(std::move(callback))
    , decodeWorker_(name, &DecoderComponent::decode, this) {

}

void DecoderComponent::decode() {
    if (decoder_ == nullptr) {
        LogE("decoder is nullptr...");
        return;
    }
    if(!packets_.empty()) {
        auto packets = packets_.detachData();
        auto frame = decoder_->send(std::move(packets));
        if (callback_) {
            callback_(std::move(frame));
        }
    } else if (isReachEnd_) {
        auto frame = decoder_->flush();
        if (callback_) {
            callback_(std::move(frame));
        }
    }
}

void DecoderComponent::receive(AVFrameArray &&packets) noexcept {
    packets_.push(packets);
}

void DecoderComponent::pause() noexcept {
    decodeWorker_.pause();
}

void DecoderComponent::resume() noexcept {
    decodeWorker_.resume();
}

void DecoderComponent::setDecoder(std::unique_ptr<IDecoder> decoder) noexcept {
    decoder_ = std::move(decoder);
}

void DecoderComponent::reset() noexcept {
    isReachEnd_ = false;
}
}
