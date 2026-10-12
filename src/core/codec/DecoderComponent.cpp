//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DecoderComponent.h"

#include <cstdlib>

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
    if (auto worker = decodeWorker_.load(); worker && worker->isCurrentThread()) {
        LogP("DecoderComponent destroyed on its decode worker thread");
        std::abort();
    }
    stop();
}

void DecoderComponent::open(DecoderType type,
                            const std::shared_ptr<DecoderConfig>& config) noexcept {
    const bool isVideo = IDecoder::isVideoDecoder(type);
    auto decoder = DecoderManager::shareInstance().create(type);
    if (!decoder) {
        LogE("create {} decoder failed", isVideo ? "video" : "audio");
        if (config_.onError) {
            config_.onError(Error::CodecUnsupportedFormat);
        }
        return;
    }
    open(std::move(decoder), config);
}

void DecoderComponent::open(std::shared_ptr<IDecoder> decoder,
                            const std::shared_ptr<DecoderConfig>& config) noexcept {
    if (!decoder || stopped_.load(std::memory_order_acquire)) {
        return;
    }
    if (auto current = decoder_.load()) {
        current->close();
    }
    decoder->setReceiveFunc([weak = weak_from_this()](DecoderData data) {
        if (auto self = weak.lock()) {
            self->config_.onData(std::move(data));
        }
    });
    auto receiver = receiver_.get();
    decoder->setWakeupCallback([receiver]() {
        if (receiver) {
            receiver->wakeup();
        }
    });
    if (!decoder->open(config)) {
        LogE("open {} decoder failed", config_.isAudio ? "audio" : "video");
        if (config_.onError) {
            config_.onError(Error::CodecConfigInvalid);
        }
        return;
    }
    decoder_.reset(decoder);
    LogI("open {} decoder success", config_.isAudio ? "audio" : "video");
    if (config_.onReady) {
        config_.onReady();
    }
}

void DecoderComponent::close() noexcept {
    if (auto worker = decodeWorker_.load(); worker && !worker->isCurrentThread()) {
        (void)worker->postAndWait([this] {
            if (auto decoder = decoder_.load()) {
                decoder->close();
            }
        });
        return;
    }
    if (auto decoder = decoder_.load()) {
        decoder->close();
    }
}

void DecoderComponent::flush() noexcept {
    flushed_.store(true, std::memory_order_release);
    if (receiver_) {
        (void)receiver_->tryReceiveAll();
    }
    auto worker = decodeWorker_.load();
    if (!worker || worker->isCurrentThread()) {
        if (auto decoder = decoder_.load()) {
            decoder->flush();
        }
    } else {
        (void)worker->postAndWait([this] {
            if (auto decoder = decoder_.load()) {
                decoder->flush();
            }
        });
    }
    if (receiver_) {
        receiver_->wakeup();
    }
    LogI("decoder flushed");
}

void DecoderComponent::start() noexcept {
    std::lock_guard lock(workerMutex_);
    if (stopped_.load(std::memory_order_acquire) || decodeWorker_.load()) {
        return;
    }
    if (!decoder_.load()) {
        return;
    }
    auto workerName = Util::genRandomName(config_.isAudio ? std::string("audioDecode_")
                                                          : std::string("videoDecode_"));
    auto receiver = receiver_.get();
    auto worker = LoopThread::create(
        std::move(workerName), [this](LoopThread& thread) { runLoop(thread); },
        [receiver] {
            if (receiver) {
                receiver->wakeup();
            }
        });
    if (!worker) {
        LogE("create decoder worker failed");
        return;
    }
    decodeWorker_.reset(worker);
    ThreadManager::shareInstance().add(worker);
}

void DecoderComponent::pause() noexcept {
    gate_.forceClose();
    if (receiver_) {
        receiver_->wakeup();
    }
}

void DecoderComponent::resume() noexcept {
    flushed_.store(false, std::memory_order_release);
    gate_.forceOpen();
    if (auto worker = decodeWorker_.load()) {
        worker->notify();
    }
}

void DecoderComponent::evaluateFeedingPauseState() noexcept {
    if (!config_.pendingFrameCount) {
        return;
    }
    gate_.update(config_.pendingFrameCount());
}

void DecoderComponent::stop() noexcept {
    std::shared_ptr<LoopThread> worker;
    {
        std::lock_guard lock(workerMutex_);
        stopped_.store(true, std::memory_order_release);
        worker = decodeWorker_.load();
    }
    if (receiver_) {
        receiver_->wakeup();
    }
    if (worker && worker->isCurrentThread()) {
        worker->stop();
        return;
    }
    if (worker) {
        worker->stop();
        worker->join();
    }
    std::call_once(finalizeOnce_, [this] {
        if (auto decoder = decoder_.load()) {
            decoder->close();
        }
        decoder_.reset();
    });
    if (worker) {
        ThreadManager::shareInstance().remove(worker);
    }
}

bool DecoderComponent::isRunning() const noexcept {
    auto worker = decodeWorker_.load();
    return worker && !worker->hasExited();
}

void DecoderComponent::drainRenderedOutputs(LoopThread& thread) noexcept {
    auto decoder = decoder_.load();
    if (!decoder) {
        return;
    }
    int32_t count = 0;
    while (decoder->hasPendingOutput() && !thread.isStopRequested()) {
        count++;
        auto res = decoder->renderOutput();
        if (res == Error::Success) {
            // skip logging successful frames to avoid log flooding
        } else if (res == Error::CodecAgain) {
            decoder->clearPendingOutput();
            break;
        } else {
            LogW("drainRenderedOutputs: renderOutput failed {} after {} frames", toString(res),
                 count);
            decoder->clearPendingOutput();
            break;
        }
    }
}

bool DecoderComponent::isFeedingPaused() const noexcept {
    return gate_.isPaused();
}

void DecoderComponent::waitForPauseResume(LoopThread& thread) noexcept {
    while (isFeedingPaused() && !thread.isStopRequested()) {
        drainRenderedOutputs(thread);
        evaluateFeedingPauseState();
        if (!isFeedingPaused()) {
            break;
        }
        if (thread.hasPendingWork()) {
            // Serve the queued flush at the loop top; staying here deadlocks the owner's resume.
            return;
        }
        // Poll instead of receiving: taking a packet would drop it, a blocking receive
        // would stall output draining.
        std::this_thread::sleep_for(kPausedPollInterval);
    }
}

AVFrameRefPtr DecoderComponent::waitForFrame(LoopThread& thread) noexcept {
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

bool DecoderComponent::decodeFrame(AVFrameRefPtr& frame, LoopThread& thread) noexcept {
    auto decoder = decoder_.load();
    int retryCount = 0;
    while (!thread.isStopRequested() && decoder) {
        auto res = decoder->decode(frame);
        if (res == Error::Success) {
            return true;
        }
        if (res == Error::CodecNotProcessed || res == Error::CodecAgain) {
            if (thread.hasPendingWork()) {
                // Queue only carries flush: drop the frame so flush runs at the loop top.
                return true;
            }
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

void DecoderComponent::runLoop(LoopThread& thread) noexcept {
    while (!thread.isStopRequested()) {
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

        auto decoder = decoder_.load();
        if (!decoder) {
            continue;
        }

        if (frame->isEOS) {
            // EOS frames also set the epoch so callbacks observe the correct epoch
            decoder->setCurrentSeekEpoch(frame->seekEpoch);
            auto res = decoder->decode(frame);
            LogI("push eos frame, res:{}", toString(res));
            // Drain any leftover output right after EOS (e.g. empty outputs the codec
            // produces for CSD config frames)
            drainRenderedOutputs(thread);
            continue;
        }

        // Set the current epoch before decoding; the decoder injects it into DecoderData
        // in callbacks
        decoder->setCurrentSeekEpoch(frame->seekEpoch);

        if (!decodeFrame(frame, thread)) {
            return;
        }

        drainRenderedOutputs(thread);
    }
}

}  // namespace slark
