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
    : callback_(callback) {
}

DecoderComponent::~DecoderComponent() {
    close();
}

bool DecoderComponent::open(DecoderType type, const std::shared_ptr<DecoderConfig>& config) noexcept {
    close();
    auto decoder = std::shared_ptr<IDecoder>(DecoderManager::shareInstance().create(type));
    if (!decoder) {
        return false;
    }
    decoder->setReceiveFunc([this](auto frame){
        callback_(std::move(frame));
    });
    decoder->setDataProvider(shared_from_this());
    if (!decoder->open(config)) {
        return false;
    }
    auto isVideo = decoder->isVideo();
    if (decoder->isPushMode()) {
        using namespace std::chrono_literals;
        auto workerName = Util::genRandomName(isVideo ?
                std::string("videoDecode_") : std::string("audioDecode_"));
        auto thread = std::make_unique<Thread>(workerName, &DecoderComponent::pushFrameDecode, this);
        thread->setInterval(5ms);
        thread->setThreadName(workerName);
        decodeWorker_.withLock([&thread](auto& worker) {
            worker = std::move(thread);
        });
    }
    decoder_.withLock([&decoder](auto& coder){
        coder = std::move(decoder);
    });
    return true;
}

void DecoderComponent::pushFrameDecode() {
    AVFramePtr frame = getDecodeFrame(true);
    if (!frame) {
        LogE("got frame is nullptr...");
        pause();
        return;
    }
    bool isSuccess = false;
    decoder_.withLock([&frame, &isSuccess](auto& decoder){
        if (!decoder || !decoder->isOpen() || !decoder->isPushMode()) {
            isSuccess = false;
            return;
        }
        decoder->decode(std::move(frame));
    });
    if (!isSuccess) {
        LogE("push decode error");
        pause();
    }
}

void DecoderComponent::send(AVFramePtr packet) noexcept {
    {
        std::lock_guard lock(mutex_);
        pendingDecodeQueue_.push_back(std::move(packet));
    }
    cond_.notify_all();
}

void DecoderComponent::pause() noexcept {
    decodeWorker_.withLock([](auto& worker) {
        if (worker) {
            worker->pause();
        }
    });
}

void DecoderComponent::start() noexcept {
    decodeWorker_.withLock([](auto& worker) {
        if (worker) {
            worker->start();
        }
    });
}

void DecoderComponent::close() noexcept {
    decoder_.withLock([](auto& coder){
        coder.reset();
    });
    decodeWorker_.withLock([](auto& worker) {
        worker.reset();
    });
}

void DecoderComponent::flush() noexcept {
    decoder_.withLock([](auto& coder){
        coder->flush();
    });
}



}
