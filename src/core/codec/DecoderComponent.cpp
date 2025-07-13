//
// Created by Nevermore on 2024/7/19.
// slark DecoderComponent
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "DecoderComponent.h"
#include "Log.hpp"
#include "Util.hpp"
#include "DecoderConfig.h"

namespace slark {

DecoderComponent::DecoderComponent(DecoderReceiveFunc&& callback)
    : callback_(callback)
    , decodeWorker_("decoder", &DecoderComponent::pushFrameDecode, this) {
    using namespace std::chrono_literals;
    decodeWorker_.setInterval(5ms);
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


AVFrameRefPtr DecoderComponent::buildEOSFrame(bool isVideo) noexcept {
    AVFrameRefPtr frame = nullptr;
    if (isVideo) {
        frame = std::make_shared<AVFrame>(AVFrameType::Video);
        auto frameInfo = std::make_shared<VideoFrameInfo>();
        frameInfo->isEndOfStream = true;
        frame->info = std::move(frameInfo);
    } else {
        frame = std::make_shared<AVFrame>(AVFrameType::Audio);
        auto frameInfo = std::make_shared<AudioFrameInfo>();
        frameInfo->isEndOfStream = true;
        frame->info = std::move(frameInfo);
    }
    return frame;
}

bool DecoderComponent::open(
    DecoderType type,
    const std::shared_ptr<DecoderConfig>& config
) noexcept {
    if (isOpened_) {
        close();
    }
    isVideo_ = IDecoder::isVideoDecoder(type);
    std::thread([type, config, this](){
        auto decoder = std::shared_ptr<IDecoder>(DecoderManager::shareInstance().create(type));
        if (!decoder) {
            return;
        }
        decoder->setReceiveFunc([this](auto frame){
            callback_(std::move(frame));
        });
        decoder->setDataProvider(shared_from_this());
        LogI("create {} decoder success", isVideo_ ? "video" : "audio");
        if (!decoder->open(config)) {
            return;
        }
        decoder_.withLock([decoder = std::move(decoder)](auto& coder) mutable {
            coder = std::move(decoder);
        });
        isOpened_ = true;
        auto workerName = Util::genRandomName(isVideo_ ?
            std::string("videoDecode_") : std::string("audioDecode_"));
        decodeWorker_.setThreadName(workerName);
        LogI("open {} decoder", isVideo_ ? "video" : "audio");
    }).detach();
    return true;
}

void DecoderComponent::pushFrameDecode() {
    if (!isOpened_) {
        LogE("{} decoder is not opened", isVideo_ ? "video" : "audio");
        return;
    }
    if (empty() && !isInputCompleted_) {
        LogE("got {} frame is nullptr", isVideo_ ? "video" : "audio");
        pause();
        return;
    }
    bool isCompleted = false;
    decoder_.withLock([&isCompleted, this]
        (auto& decoder) mutable {
        if (!decoder || !decoder->isOpen() || decoder->isCompleted()) {
            if (decoder) {
                isCompleted = decoder->isCompleted();
            }
            return;
        }
        if (isInputCompleted_ && !decoder->isCompleted()){
            auto eosFrame = buildEOSFrame(decoder->isVideo());
            decoder->decode(eosFrame);
            LogI("push eos frame, {}", isVideo_ ? "video" : "audio");
            return;
        }
        AVFrameRefPtr frame;
        do {
            frame = peekDecodeFrame();
            if (!frame) {
                break;
            }
            auto decodeRes = static_cast<int>(decoder->decode(frame));
            if (decodeRes < 0) {
                LogE("decode error:{}", decodeRes);
                break;
            }
            {
                std::lock_guard lock(mutex_);
                pendingDecodeQueue_.pop_front();
            }

        } while (frame->isFastPushFrame()); //fast push
    });
    if (isCompleted) {
        pause();
        LogI("{} decode pause, completed", isVideo_ ? "video" : "audio");
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
    if (!isOpened_) {
        return;
    }
    decoder_.withLock([](auto& coder){
        if (coder) {
            coder->close();
        }
        coder.reset();
    });
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
    LogI("flushed");
    isInputCompleted_ = false;
}

bool DecoderComponent::isDecodeCompleted() noexcept {
    bool isCompleted_ = false;
    decoder_.withLock([&isCompleted_](auto& coder){
        if (coder) {
            isCompleted_ = coder->isCompleted();
        }
    });
    return isCompleted_;
}

}
