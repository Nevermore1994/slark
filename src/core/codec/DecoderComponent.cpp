//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DecoderComponent.h"

#include "DecoderConfig.h"
#include "Log.hpp"
#include "Util.h"

namespace slark {

DecoderComponent::DecoderComponent(DecoderComponentConfig config)
    : config_(std::move(config)), decodeWorker_(Thread::create("decoder")) {}

DecoderComponent::~DecoderComponent() {
    close();
}

void DecoderComponent::open(DecoderType type,
                            const std::shared_ptr<DecoderConfig>& config) noexcept {
    if (isOpened_) {
        close();
    }
    const bool isVideo = IDecoder::isVideoDecoder(type);
    auto self = shared_from_this();
    decodeWorker_->schedule([type, config, isVideo, self]() {
        auto decoder = DecoderManager::shareInstance().create(type);
        if (!decoder) {
            LogE("create {} decoder failed", isVideo ? "video" : "audio");
            if (self->config_.onError) {
                self->config_.onError(Error::CodecUnsupportedFormat);
            }
            return;
        }
        decoder->setReceiveFunc([weak = std::weak_ptr(self)](DecoderData data) {
            if (auto s = weak.lock()) {
                s->config_.onData(std::move(data));
            }
        });
        if (!decoder->open(config)) {
            LogE("open {} decoder failed", isVideo ? "video" : "audio");
            if (self->config_.onError) {
                self->config_.onError(Error::CodecConfigInvalid);
            }
            return;
        }
        self->decoder_.withLock([&decoder](auto& coder) {
            coder = std::move(decoder);
        });
        self->isOpened_ = true;
        auto workerName = Util::genRandomName(isVideo ? std::string("videoDecode_")
                                                      : std::string("audioDecode_"));
        self->decodeWorker_->setThreadName(workerName);
        LogI("open {} decoder success", isVideo ? "video" : "audio");
        if (self->config_.onReady) {
            self->config_.onReady();
        }
    });
}

void DecoderComponent::close() noexcept {
    if (!isOpened_) {
        return;
    }
    isOpened_ = false;
    decodeWorker_->pause();
    decoder_.withLock([](auto& coder) {
        if (coder) {
            coder->close();
            coder.reset();
        }
    });
}

void DecoderComponent::flush() noexcept {
    decoder_.withLock([](auto& coder) {
        if (coder) {
            coder->flush();
        }
    });
    LogI("flushed");
}

void DecoderComponent::start() noexcept {
    decodeWorker_->start();
    decodeWorker_->schedule([this]() {
        pushFrameDecode();
    });
}

void DecoderComponent::pause() noexcept {
    decodeWorker_->pause();
}

void DecoderComponent::scheduleDecoding() noexcept {
    decodeWorker_->schedule([this]() {
        pushFrameDecode();
    });
}

bool DecoderComponent::isRunning() const noexcept {
    return decodeWorker_->isRunning();
}

void DecoderComponent::pushFrameDecode() {
    if (!isOpened_) {
        return;
    }
    auto decoder = decoder_.withLock([](auto& coder) -> std::shared_ptr<IDecoder> {
        return coder;
    });
    if (!decoder || !decoder->isOpen() || decoder->isCompleted()) {
        return;
    }
    while (isOpened_) {
        if (!config_.dataProvider) {
            break;
        }
        auto [frame, providerErr] = config_.dataProvider();
        if (providerErr != Error::Success) {
            LogE("dataProvider error: {}", toString(providerErr));
            if (config_.onError) {
                config_.onError(providerErr);
            }
            break;
        }
        if (!frame) {
            break;
        }
        if (frame->isEOS) {
            decoder->decode(frame);
            LogI("push eos frame");
            break;
        }
        auto res = decoder->decode(frame);
        if (res != Error::Success && res != Error::CodecAgain) {
            LogE("decode error: {}", toString(res));
            if (config_.onError) {
                config_.onError(res);
            }
            break;
        }
        if (!frame->isFastPush) {
            break;
        }
    }
}

} // namespace slark
