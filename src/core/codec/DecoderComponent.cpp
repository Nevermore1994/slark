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
    , decodeWorker_("decoder", &DecoderComponent::pushFrameDecode, this){
}

DecoderComponent::~DecoderComponent() {
    close();
}

AVFrameRefPtr DecoderComponent::requestDecodeFrame(bool isBlocking) noexcept {
    AVFrameRefPtr frame;
    if (isBlocking) {
        std::unique_lock lock(mutex_);
        cond_.wait(lock, [this]{
            return !pendingDecodeQueue_.empty() || !isOpened_;
        });
        if (!isOpened_) {
            LogE("decoder closed");
            return nullptr;
        }
        frame = std::move(pendingDecodeQueue_.front());
        pendingDecodeQueue_.pop_front();
    } else {
        std::lock_guard lock(mutex_);
        if (!pendingDecodeQueue_.empty()) {
            return nullptr;
        }
        frame = std::move(pendingDecodeQueue_.front());
        pendingDecodeQueue_.pop_front();
    }
    return frame;
}

AVFrameRefPtr DecoderComponent::peekDecodeFrame() noexcept {
    std::lock_guard lock(mutex_);
    if (pendingDecodeQueue_.empty()) {
        return nullptr;
    }
    return pendingDecodeQueue_.front();
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
    using namespace std::chrono_literals;
    auto workerName = Util::genRandomName(isVideo ?
            std::string("videoDecode_") : std::string("audioDecode_"));
    decodeWorker_.setInterval(5ms);
    decodeWorker_.setThreadName(workerName);
    decoder_.withLock([&decoder](auto& coder){
        coder = std::move(decoder);
    });
    return true;
}

void DecoderComponent::pushFrameDecode() {
    auto frame = peekDecodeFrame();
    if (!frame) {
        LogE("got frame is nullptr");
        pause();
        return;
    }
    bool isPushed = false;
    decoder_.withLock([&frame, &isPushed](auto& decoder){
        if (!decoder || !decoder->isOpen()) {
            return;
        }
        if (auto code = decoder->decode(frame); code == DecoderErrorCode::Success) {
            isPushed = true;
        } else {
            LogE("push frame error:{}", static_cast<int>(code));
        }
    });
    if (isPushed) {
        std::lock_guard lock(mutex_);
        pendingDecodeQueue_.pop_front();
    }
}

void DecoderComponent::send(AVFramePtr packet) noexcept {
    {
        std::lock_guard lock(mutex_);
        pendingDecodeQueue_.emplace_back(std::move(packet));
    }
    cond_.notify_one();
    decodeWorker_.start();
}

void DecoderComponent::pause() noexcept {
    decodeWorker_.pause();
}

void DecoderComponent::start() noexcept {
    decodeWorker_.start();
}

void DecoderComponent::close() noexcept {
    decoder_.withLock([](auto& coder){
        coder.reset();
    });
    {
        std::lock_guard lock(mutex_);
        pendingDecodeQueue_.clear();
    }
    isOpened_ = false;
    cond_.notify_all();
}

void DecoderComponent::flush() noexcept {
    decoder_.withLock([](auto& coder){
        if (coder) {
            coder->flush();
        }
    });
    {
        std::lock_guard lock(mutex_);
        pendingDecodeQueue_.clear();
    }
}


}
