//
// Created by Nevermore on 2023/7/19.
// slark DecoderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "DecoderComponent.h"
#include "Log.hpp"
#include "Util.hpp"
#include "DecoderConfig.h"

namespace slark {

DecoderComponent::DecoderComponent(DecoderReceiveFunc&& callback)
    : callback_(callback)
    , decodeWorker_(Util::genRandomName("DecodeThread_"), &DecoderComponent::decode, this) {
}

DecoderComponent::~DecoderComponent() {
    close();
}

bool DecoderComponent::open(DecoderType type, std::shared_ptr<DecoderConfig> config) noexcept {
    decoder_ = DecoderManager::shareInstance().create(type);
    if (!decoder_) {
        return false;
    }
    decoder_->receiveFunc = [this](auto frame){
        callback_(std::move(frame));
    };
    if (decoder_->isVideo()) {
        auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config);
        if (!videoConfig) {
            return false;
        }
        return decoder_->open(videoConfig);
    } else if (decoder_->isAudio()) {
        auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config);
        if (!audioConfig) {
            return false;
        }
        return decoder_->open(audioConfig);
    }
    using namespace std::chrono_literals;
    decodeWorker_.setInterval(2ms);
    return true;
}

void DecoderComponent::decode() {
    if (decoder_ == nullptr) {
        LogE("decoder is nullptr...");
        return;
    }
    AVFramePtr packet;
    packets_.withWriteLock([&](auto& deque){
        if (!deque.empty()) {
            packet = std::move(deque.front());
            deque.pop_front();
        }
    });
    if(packet) {
        std::unique_lock<std::mutex> lock(mutex_);
        decoder_->send(std::move(packet));
    } else if (isReachEnd_ && !decoder_->isFlushed()) {
        std::unique_lock<std::mutex> lock(mutex_);
        decoder_->flush();
    }
}

void DecoderComponent::send(AVFramePtr packet) noexcept {
    packets_.withWriteLock([&](auto& deque){
        packet->stats.prepareDecodeStamp = Time::nowTimeStamp();
        deque.emplace_back(std::move(packet));
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
    std::unique_lock<std::mutex> lock(mutex_);
    decodeWorker_.pause();
    decoder_.reset();
    isReachEnd_ = false;
    packets_.withWriteLock([](auto& vec){
        vec.clear();
    });
}

void DecoderComponent::close() noexcept {
    reset();
    decodeWorker_.stop();
}

void DecoderComponent::flush() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    if (decoder_) {
        decoder_->flush();
    }
    packets_.withWriteLock([](auto& vec){
        vec.clear();
    });
}

}
