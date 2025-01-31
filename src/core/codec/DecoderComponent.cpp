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
    , decodeWorker_(&DecoderComponent::decode, this) {
}

DecoderComponent::~DecoderComponent() {
    close();
}

AVFramePtr DecoderComponent::getDecodeFrame() noexcept {
    std::unique_lock lock(frameMutex_);
    cond_.wait(lock, [this](){
        return !pendingDecodePacketQueue_.empty() || !decoder_;
    });
    if (!decoder_) {
        LogI("decoder closed");
        return nullptr;
    }
    auto frame = std::move(pendingDecodePacketQueue_.front());
    pendingDecodePacketQueue_.pop_front();
    return frame;
}

bool DecoderComponent::open(DecoderType type, std::shared_ptr<DecoderConfig> config) noexcept {
    std::lock_guard lock(decoderMutex_);
    decoder_ = DecoderManager::shareInstance().create(type);
    if (!decoder_) {
        return false;
    }
    decoder_->receiveFunc = [this](auto frame){
        callback_(std::move(frame));
    };
    decoder_->setProvider(weak_from_this());
    if (decoder_->isVideo()) {
        auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config);
        if (!videoConfig) {
            return false;
        }
        decodeWorker_.setThreadName(Util::genRandomName("videoDecode_"));
        return decoder_->open(videoConfig);
    } else if (decoder_->isAudio()) {
        auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config);
        if (!audioConfig) {
            return false;
        }
        decodeWorker_.setThreadName(Util::genRandomName("audioDecode_"));
        return decoder_->open(audioConfig);
    }
    using namespace std::chrono_literals;
    decodeWorker_.setInterval(5ms);
    return true;
}

void DecoderComponent::decode() {
    auto frame = getDecodeFrame();
    std::lock_guard lock(decoderMutex_);
    if (decoder_ == nullptr || !decoder_->isOpen()) {
        LogE("decoder is nullptr...");
        decodeWorker_.pause();
        return;
    }
    decoder_->send(std::move(frame));
}

void DecoderComponent::send(AVFramePtr packet) noexcept {
    {
        std::lock_guard lock(frameMutex_);
        pendingDecodePacketQueue_.push_back(std::move(packet));
    }
    cond_.notify_all();
    decodeWorker_.start();
}

void DecoderComponent::pause() noexcept {
    decodeWorker_.pause();
}

void DecoderComponent::start() noexcept {
    decodeWorker_.start();
}

void DecoderComponent::reset() noexcept {
    decodeWorker_.pause();
    {
        std::lock_guard lock(frameMutex_);
        pendingDecodePacketQueue_.clear();
    }
    {
        std::lock_guard lock(decoderMutex_);
        decoder_.reset();
    }
    isReachEnd_ = false;
    cond_.notify_all();
}

void DecoderComponent::close() noexcept {
    reset();
    decodeWorker_.stop();
}

void DecoderComponent::flush() noexcept {
    std::lock_guard lock(decoderMutex_);
    if (decoder_) {
        decoder_->flush();
    }
    cond_.notify_all();
}

}
