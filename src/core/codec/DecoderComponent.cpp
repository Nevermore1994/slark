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
    decoder->setWakeupCallback([weak = weak_from_this()]() {
        if (auto s = weak.lock()) {
            if (s->receiver_) {
                s->receiver_->wakeup();
            }
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
}

void DecoderComponent::flush() noexcept {
    flushed_.store(true, std::memory_order_release);
    if (receiver_) {
        (void)receiver_->tryReceiveAll();
    }
    if (decoder_) {
        decoder_->flush();
    }
    if (receiver_) {
        receiver_->wakeup();
    }
    LogI("decoder flushed");
}

void DecoderComponent::start() noexcept {
    if (decodeWorker_) {
        decodeWorker_->start();
        return;
    }
    if (!decoder_) {
        return;
    }
    auto workerName = Util::genRandomName(config_.isAudio ? std::string("audioDecode_")
                                                          : std::string("videoDecode_"));
    decodeWorker_ = Thread::createWithLoop(std::move(workerName), [this](Thread& thread) {
        runLoop(thread);
    });
    ThreadManager::shareInstance().add(decodeWorker_);
}

void DecoderComponent::pause() noexcept {
    if (decodeWorker_) {
        decodeWorker_->pause();
    }
    if (receiver_) {
        receiver_->wakeup();
    }
}

void DecoderComponent::resume() noexcept {
    flushed_.store(false, std::memory_order_release);
    if (decodeWorker_) {
        decodeWorker_->start();
    }
}

void DecoderComponent::stop() noexcept {
    if (receiver_) {
        receiver_->wakeup();
    }
    if (decodeWorker_) {
        decodeWorker_->stop();
        decodeWorker_->join();  // 同步等 worker 退出，避免 loop 仍在跑而成员被 reset 导致 UAF
        ThreadManager::shareInstance().remove(decodeWorker_);
        decodeWorker_.reset();
    }
    close();
    decoder_.reset();
}

bool DecoderComponent::isRunning() const noexcept {
    return decodeWorker_ && !decodeWorker_->isExit();
}

void DecoderComponent::processPendingOutputs(Thread& thread) noexcept {
    if (!decoder_) {
        return;
    }
    int32_t count = 0;
    while (decoder_->hasPendingOutput() && !thread.isExit()) {
        count++;
        auto res = decoder_->renderOutput();
        if (res == Error::Success) {
            LogD("processPendingOutputs: rendered frame count={}", count);
        } else if (res == Error::CodecAgain) {
            LogD("processPendingOutputs: CodecAgain after {} frames, clear pending", count);
            decoder_->clearPendingOutput();
            break;
        } else {
            LogW("processPendingOutputs: renderOutput failed {} after {} frames", toString(res), count);
            decoder_->clearPendingOutput();
            break;
        }
    }
}

AVFrameRefPtr DecoderComponent::waitForFrame(Thread& thread) noexcept {
    auto exp = receiver_->receive();
    if (!exp.has_value()) {
        if (exp.error() == ChannelEventType::Awakened) {
            LogD("waitForFrame: Awakened, process pending outputs");
            processPendingOutputs(thread);
        }
        flushed_.store(false, std::memory_order_release);
        return nullptr;
    }
    // flush 兜底：清空窗口期残帧
    if (flushed_.load(std::memory_order_acquire)) {
        LogD("waitForFrame: flushed, drain channel");
        (void)receiver_->tryReceiveAll();
        flushed_.store(false, std::memory_order_release);
        return nullptr;
    }
    return std::move(*exp);
}

bool DecoderComponent::decodeFrame(AVFrameRefPtr& frame, Thread& thread) noexcept {
    int retryCount = 0;
    while (!thread.isExit() && decoder_) {
        auto res = decoder_->decode(frame);
        if (res == Error::Success) {
            return true;
        }
        if (res == Error::CodecNotProcessed || res == Error::CodecAgain) {
            retryCount++;
            if (retryCount > kMaxDecodeRetry) {
                LogE("decode retry exhausted after {} retries, pts:{}", kMaxDecodeRetry, frame->pts);
                if (config_.onError) {
                    config_.onError(Error::CodecResourceExhausted);
                }
                return false;
            }
            auto backoff = std::chrono::milliseconds(1) * (1 << std::min(retryCount, 4));
            backoff = std::min(backoff, kMaxDecodeBackoff);
            std::this_thread::sleep_for(backoff);
            processPendingOutputs(thread);
        } else {
            if (thread.isExit() &&
                res == Error::CodecNotStarted) {
                break;
            }
            LogE("decode error: {}", toString(res));
            if (config_.onError) {
                config_.onError(res);
            }
            return false;
        }
    }
    return true; // stop requested, not an error
}

void DecoderComponent::runLoop(Thread& thread) noexcept {
    while (!thread.shouldStop()) {
        thread.drainPendingWork();

        processPendingOutputs(thread);

        auto frame = waitForFrame(thread);
        if (!frame) {
            continue;
        }

        if (!decoder_) {
            continue;
        }

        if (frame->isEOS) {
            // EOS 帧也设置 epoch，确保回调时 epoch 正确
            if (decoder_) {
                decoder_->setCurrentSeekEpoch(frame->seekEpoch);
            }
            auto res = decoder_->decode(frame);
            LogI("push eos frame, res:{}", toString(res));
            // 推送 EOS 后立即排空可能的残留输出（如 CSD config 帧触发 codec 产出的空输出）
            processPendingOutputs(thread);
            continue;
        }

        // 在 decode 前设置当前 epoch，decoder 回调时将其注入 DecoderData
        if (decoder_) {
            decoder_->setCurrentSeekEpoch(frame->seekEpoch);
        }

        if (!decodeFrame(frame, thread)) {
            return;
        }

        processPendingOutputs(thread);
    }
}

} // namespace slark
