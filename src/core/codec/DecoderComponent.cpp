//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DecoderComponent.h"

#include "DecoderConfig.h"
#include "Log.hpp"
#include "Util.h"

namespace slark {

DecoderComponent::DecoderComponent(ReceiverPtr<AVFrameRefPtr> receiver,
                                   DecoderComponentConfig config)
    : config_(std::move(config)), receiver_(std::move(receiver)) {}

DecoderComponent::~DecoderComponent() {
    stop();
}

void DecoderComponent::open(DecoderType type,
                            const std::shared_ptr<DecoderConfig>& config) noexcept {
    if (decoder_) {
        close();
    }
    const bool isVideo = IDecoder::isVideoDecoder(type);
    auto decoder = DecoderManager::shareInstance().create(type);
    if (!decoder) {
        LogE("create {} decoder failed", isVideo ? "video" : "audio");
        if (config_.onError) {
            config_.onError(Error::CodecUnsupportedFormat);
        }
        return;
    }
    decoder->setReceiveFunc([weak = weak_from_this()](DecoderData data) {
        if (auto s = weak.lock()) {
            s->config_.onData(std::move(data));
        }
    });
    if (!decoder->open(config)) {
        LogE("open {} decoder failed", isVideo ? "video" : "audio");
        if (config_.onError) {
            config_.onError(Error::CodecConfigInvalid);
        }
        return;
    }
    decoder_ = std::move(decoder);
    LogI("open {} decoder success", isVideo ? "video" : "audio");
    if (config_.onReady) {
        config_.onReady();
    }
}

void DecoderComponent::close() noexcept {
    if (!decoder_) {
        return;
    }
    decoder_->close();
    decoder_.reset();
}

void DecoderComponent::flush() noexcept {
    flushed_.store(true, std::memory_order_release);
    if (receiver_) {
        (void)receiver_->tryReceiveAll();
    }
    if (decoder_) {
        decoder_->flush();
    }
    pauseCv_.notify_all();
    if (receiver_) {
        receiver_->wakeup();
    }
    LogI("decoder flushed");
}

void DecoderComponent::start() noexcept {
    if (!decoder_ || decodeWorker_) {
        return;
    }
    auto workerName = Util::genRandomName(config_.isAudio ? std::string("audioDecode_")
                                                          : std::string("videoDecode_"));
    decodeWorker_ = Thread::createWithRunLoop(std::move(workerName), [this] {
        runLoop();
    });
    ThreadManager::shareInstance().add(decodeWorker_);
}

void DecoderComponent::pause() noexcept {
    std::lock_guard<std::mutex> lock(pauseMutex_);
    paused_ = true;
}

void DecoderComponent::resume() noexcept {
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        paused_ = false;
    }
    pauseCv_.notify_all();
}

void DecoderComponent::stop() noexcept {
    stopRequested_.store(true, std::memory_order_release);
    pauseCv_.notify_all();
    if (receiver_) {
        receiver_->wakeup();
    }
    if (decodeWorker_) {
        ThreadManager::shareInstance().remove(decodeWorker_);
        decodeWorker_.reset();
    }
    close();
}

bool DecoderComponent::isRunning() const noexcept {
    return decodeWorker_ && decodeWorker_->isRunning();
}

void DecoderComponent::runLoop() noexcept {
    while (!stopRequested_.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lock(pauseMutex_);
            if (paused_ && !stopRequested_.load(std::memory_order_acquire)) {
                Thread::ScopedInactive guard; // 阻塞等待时标记非活跃，防止 ThreadManager 误判卡死
                pauseCv_.wait(lock, [this] {
                    return stopRequested_.load(std::memory_order_acquire) || !paused_;
                });
            }
        }
        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }

        auto exp = receiver_->receive();

        if (!exp.has_value()) {
            if (exp.error() == ChannelEventType::Awakened) {
                continue;
            }
            break;
        }

        // flush 兜底：清空窗口期残帧
        if (flushed_.load(std::memory_order_acquire)) {
            (void)receiver_->tryReceiveAll();
            flushed_.store(false, std::memory_order_release);
            continue;
        }

        auto frame = std::move(*exp);

        if (!decoder_) {
            continue;
        }

        if (frame->isEOS) {
            (void)decoder_->decode(frame);
            LogI("push eos frame");
            continue;
        }

        auto res = decoder_->decode(frame);
        if (res != Error::Success && res != Error::CodecAgain) {
            LogE("decode error: {}", toString(res));
            if (config_.onError) {
                config_.onError(res);
            }
            break;
        }
    }
}

} // namespace slark
