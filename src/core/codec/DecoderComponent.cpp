//
// Created by Nevermore on 2023/7/19.
// slark DecoderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "DecoderComponent.h"
#include "Log.hpp"
#include "Utility.hpp"

namespace slark {

DecoderComponent::DecoderComponent(DecoderType decodeType, DecoderReceiveFunc&& callback)
    : decoder_(DecoderManager::shareInstance().create(decodeType))
    , callback_(std::move(callback))
    , decodeWorker_(Util::genRandomName("DecodeThread_"), &DecoderComponent::decode, this) {

}

void DecoderComponent::decode() {
    if (decoder_ == nullptr) {
        LogE("decoder is nullptr...");
        return;
    }
    AVFrameArray packets;
    packets_.withLock([&](auto& vec){
        packets.swap(vec);
    });
    AVFrameArray decodeFrameArray;
    if(!packets.empty()) {;
        decodeFrameArray = decoder_->send(std::move(packets));
    } else if (isReachEnd_) {
        auto frame = decoder_->flush();
    } else {
        decodeWorker_.pause();
        return;
    }
    if (callback_) {
        callback_(std::move(decodeFrameArray));
    }
}

void DecoderComponent::send(AVFrameArray&& packets) noexcept {
    packets_.withLock([&](auto& vec){
        std::move(packets.begin(), packets.end(), std::back_inserter(vec));
    });
    decodeWorker_.resume();
}

void DecoderComponent::send(AVFramePtr packet) noexcept {
    packets_.withLock([&](auto& vec){
        vec.emplace_back(std::move(packet));
    });
    decodeWorker_.resume();
}

void DecoderComponent::pause() noexcept {
    decodeWorker_.pause();
}

void DecoderComponent::resume() noexcept {
    decodeWorker_.resume();
}

void DecoderComponent::reset() noexcept {
    decodeWorker_.pause();
    isReachEnd_ = false;
    packets_.withLock([](auto& vec){
        vec.clear();
    });
}

void DecoderComponent::close() noexcept {
    decodeWorker_.stop();
    isReachEnd_ = false;
    packets_.withLock([](auto& vec){
        vec.clear();
    });
}

}
