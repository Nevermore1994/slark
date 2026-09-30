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
    : config_(std::move(config)),
      receiver_(std::move(receiver)),
      gate_(WatermarkConfig<int32_t>{config_.backpressure.lowWatermarkFrames,
                                     config_.backpressure.highWatermarkFrames},
            {}) {}

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
    gate_.forceClose();
    if (receiver_) {
        receiver_->wakeup();
    }
}

void DecoderComponent::resume() noexcept {
    flushed_.store(false, std::memory_order_release);
    // pause() never suspends the worker, so there is no thread to restart here.
    gate_.forceOpen();
}

void DecoderComponent::evaluateFeedingPauseState() noexcept {
    if (!config_.pendingFrameCount) {
        return;
    }
    gate_.update(config_.pendingFrameCount());
}

void DecoderComponent::stop() noexcept {
    if (receiver_) {
        receiver_->wakeup();
    }
    if (decodeWorker_) {
        decodeWorker_->stop();
        decodeWorker_->join();  // synchronously wait for the worker to exit, avoiding UAF if
                                // members are reset while the loop still runs
        ThreadManager::shareInstance().remove(decodeWorker_);
        decodeWorker_.reset();
    }
    close();
    decoder_.reset();
}

bool DecoderComponent::isRunning() const noexcept {
    return decodeWorker_ && !decodeWorker_->isStopRequested();
}

void DecoderComponent::drainRenderedOutputs(Thread& thread) noexcept {
    if (!decoder_) {
        return;
    }
    int32_t count = 0;
    while (decoder_->hasPendingOutput() && !thread.isStopRequested()) {
        count++;
        auto res = decoder_->renderOutput();
        if (res == Error::Success) {
            // skip logging successful frames to avoid log flooding
        } else if (res == Error::CodecAgain) {
            decoder_->clearPendingOutput();
            break;
        } else {
            LogW("drainRenderedOutputs: renderOutput failed {} after {} frames", toString(res),
                 count);
            decoder_->clearPendingOutput();
            break;
        }
    }
}

bool DecoderComponent::isFeedingPaused() const noexcept {
    return gate_.isPaused();
}

void DecoderComponent::waitForPauseResume(Thread& thread) noexcept {
    while (isFeedingPaused() && !thread.isStopRequested()) {
        drainRenderedOutputs(thread);
        evaluateFeedingPauseState();
        if (!isFeedingPaused()) {
            break;
        }
        // Poll instead of receiving: taking a packet would drop it, a blocking receive
        // would stall output draining.
        std::this_thread::sleep_for(kPausedPollInterval);
    }
}

AVFrameRefPtr DecoderComponent::waitForFrame(Thread& thread) noexcept {
    auto exp = receiver_->receive();
    if (!exp.has_value()) {
        if (exp.error() == ChannelEventType::Awakened) {
            LogD("waitForFrame: Awakened, process pending outputs");
            drainRenderedOutputs(thread);
        }
        flushed_.store(false, std::memory_order_release);
        return nullptr;
    }
    // Flush fallback: drain leftover frames from the window period
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
    while (!thread.shouldStop() && decoder_) {
        auto res = decoder_->decode(frame);
        if (res == Error::Success) {
            return true;
        }
        if (res == Error::CodecNotProcessed || res == Error::CodecAgain) {
            retryCount++;
            if (retryCount > kMaxDecodeRetry) {
                LogE("decode retry exhausted after {} retries, pts:{}", kMaxDecodeRetry,
                     frame->pts);
                if (config_.onError) {
                    config_.onError(Error::CodecResourceExhausted);
                }
                return false;
            }
            auto backoff = std::chrono::milliseconds(1) * (1 << std::min(retryCount, 4));
            backoff = std::min(backoff, kMaxDecodeBackoff);
            std::this_thread::sleep_for(backoff);
            drainRenderedOutputs(thread);
        } else {
            if (thread.isStopRequested() && res == Error::CodecNotStarted) {
                break;
            }
            LogE("decode error: {}", toString(res));
            if (config_.onError) {
                config_.onError(res);
            }
            return false;
        }
    }
    return true;  // stop requested, not an error
}

void DecoderComponent::runLoop(Thread& thread) noexcept {
    while (!thread.shouldStop()) {
        thread.drainPendingWork();

        drainRenderedOutputs(thread);

        evaluateFeedingPauseState();

        if (isFeedingPaused()) {
            waitForPauseResume(thread);
            continue;
        }

        auto frame = waitForFrame(thread);
        if (!frame) {
            continue;
        }

        if (!decoder_) {
            continue;
        }

        if (frame->isEOS) {
            // EOS frames also set the epoch so callbacks observe the correct epoch
            if (decoder_) {
                decoder_->setCurrentSeekEpoch(frame->seekEpoch);
            }
            auto res = decoder_->decode(frame);
            LogI("push eos frame, res:{}", toString(res));
            // Drain any leftover output right after EOS (e.g. empty outputs the codec
            // produces for CSD config frames)
            drainRenderedOutputs(thread);
            continue;
        }

        // Set the current epoch before decoding; the decoder injects it into DecoderData
        // in callbacks
        if (decoder_) {
            decoder_->setCurrentSeekEpoch(frame->seekEpoch);
        }

        if (!decodeFrame(frame, thread)) {
            return;
        }

        drainRenderedOutputs(thread);
    }
}

}  // namespace slark
