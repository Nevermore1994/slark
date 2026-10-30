//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AudioPipeline.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>

#include "AudioSink.h"
#include <format>

#include "Log.hpp"
#include "MediaClock.h"

namespace slark::engine {
namespace {

using namespace std::chrono_literals;
constexpr auto kWakeupTick = 20ms;
// At one tick per try this bounds a wedged codec to about 2s.
constexpr int kMaxDecodeRetries = 100;

}  // namespace

AudioPipeline::AudioPipeline(Receiver<AVFrameRefPtr>& input,
                     DecoderFactory decoderFactory,
                     std::unique_ptr<AudioSink> sink,
                     SeekEpoch epoch,
                     Time::TimePoint target,
                     std::function<void()> wake)
    : input_(input),
      wake_(std::move(wake)),
      decoderFactory_(std::move(decoderFactory)),
      sink_(std::move(sink)),
      epoch_(epoch),
      target_(target) {
    // Created before the pipeline thread starts so command threads never race the assignment.
    decoder_ = decoderFactory_ ? decoderFactory_() : nullptr;
    if (!decoder_) {
        fail(Error::CodecError);
        return;
    }
    decoder_->setReceiveFunc([this](DecoderData data) {
        handleDecoded(std::move(data));
    });
    thread_ = std::thread([this] {
        run();
    });
}

AudioPipeline::~AudioPipeline() {
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void AudioPipeline::flush(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) {
    {
        std::lock_guard lock(mutex_);
        epoch_ = epoch;
        target_ = target;
        seekMode_ = mode;
        eosReceived_ = false;
        decoderEnded_ = false;
        lastDecodedPts_.reset();
        primedOnce_ = false;
        lastAnchored_ = target;
        pendingOutput_.clear();
        (void)input_.tryReceiveAll();
        if (sink_) {
            sink_->flush(target);
        }
    }
    if (decoder_) {
        decoder_->flush();
    }
    cv_.notify_all();
}

void AudioPipeline::retarget(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) {
    std::lock_guard lock(mutex_);
    epoch_ = epoch;
    target_ = target;
    seekMode_ = mode;
}

void AudioPipeline::setClock(MediaClock& clock) {
    std::lock_guard lock(mutex_);
    clock_ = &clock;
}

void AudioPipeline::setPlaying(bool playing) {
    {
        std::lock_guard lock(mutex_);
        if (playing_ == playing || !sink_) {
            return;
        }
        playing_ = playing;
    }
    if (playing) {
        sink_->start();
    } else {
        sink_->pause();
    }
}

void AudioPipeline::setVolume(float volume) {
    if (sink_) {
        sink_->setVolume(volume);
    }
}

bool AudioPipeline::ready(SeekEpoch epoch, Time::TimePoint target) const {
    std::lock_guard lock(mutex_);
    if (epoch != epoch_) {
        return false;
    }
    if (eosReceived_ && decoderEnded_) {
        return true;
    }
    if (!lastDecodedPts_ || *lastDecodedPts_ < target.second()) {
        return false;
    }
    if (!primedOnce_ && sink_->primed()) {
        primedOnce_ = true;
    }
    return primedOnce_;
}

bool AudioPipeline::ended() const {
    std::lock_guard lock(mutex_);
    return eosReceived_ && decoderEnded_ && pendingOutput_.empty() && sink_->drained();
}

std::optional<Error> AudioPipeline::failure() const {
    std::lock_guard lock(mutex_);
    return failure_;
}

void AudioPipeline::run() {
    while (true) {
        {
            std::lock_guard lock(mutex_);
            if (closed_ || failure_) {
                // A failed pipeline must not keep feeding the dead codec.
                return;
            }
        }
        for (auto& frame : input_.tryReceiveAll()) {
            handleInput(frame);
            std::lock_guard lock(mutex_);
            if (failure_) {
                return;
            }
        }
        drainDecoderOutput();
        pump();
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, kWakeupTick);
    }
}

void AudioPipeline::handleInput(AVFrameRefPtr& frame) {
    if (!frame || frame->isEOS) {
        if (frame && frame->isEOS) {
            {
                std::lock_guard lock(mutex_);
                eosReceived_ = true;
            }
            if (wake_) {
                wake_();
            }
        }
        return;
    }
    std::optional<Time::TimePoint> target;
    SeekMode mode = SeekMode::Exact;
    {
        std::lock_guard lock(mutex_);
        if (frame->seekEpoch != epoch_) {
            return;
        }
        target = target_;
        mode = seekMode_;
    }
    const auto pts = frame->ptsTime();
    if (mode == SeekMode::Exact && !frame->isDiscard && pts < target->second()) {
        return;
    }
    if (!decodeOne(frame)) {
        return;
    }
    drainDecoderOutput();
    pump();
}

bool AudioPipeline::decodeOne(AVFrameRefPtr& frame) {
    // A codec that refuses input without producing output is dead: fail instead of waiting.
    int againRetries = 0;
    while (true) {
        {
            std::unique_lock lock(mutex_);
            if (closed_) {
                return false;
            }
            if (pendingOutput_.size() >= kMaxPendingOutput) {
                // The sink is the bottleneck: pump here and wait only if nothing could be written.
                lock.unlock();
                pump();
                lock.lock();
                if (closed_) {
                    return false;
                }
                if (pendingOutput_.size() >= kMaxPendingOutput) {
                    cv_.wait_for(lock, kWakeupTick);
                }
                continue;
            }
        }
        // Platform decoders stamp their output with this epoch; otherwise frames drop as stale.
        decoder_->setCurrentSeekEpoch(frame->seekEpoch);
        const auto error = decoder_->decode(frame);
        if (error == Error::Success) {
            return true;
        }
        if (error != Error::CodecAgain && error != Error::CodecNotProcessed) {
            fail(error);
            return false;
        }
        if (++againRetries > kMaxDecodeRetries) {
            LogE("audio decode retry exhausted after {}, pts:{:.3f}", againRetries,
                 frame->ptsTime());
            fail(Error::CodecResourceExhausted);
            return false;
        }
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, kWakeupTick);
    }
}

void AudioPipeline::handleDecoded(slark::DecoderData data) {
    if (!data.frame || data.error != Error::Success) {
        if (data.error != Error::Success && data.error != Error::Cancelled) {
            fail(data.error);
        }
        return;
    }
    // Prefer DecoderData.seekEpoch; the frame's own copy is only a fallback.
    const auto epoch = data.seekEpoch != 0 ? data.seekEpoch : data.frame->seekEpoch;
    std::lock_guard lock(mutex_);
    if (closed_ || data.frame->isEOS || data.frame->isDiscard || epoch != epoch_) {
        return;
    }
    data.frame->seekEpoch = epoch;
    const auto pts = data.frame->ptsTime();
    if (!lastDecodedPts_ || pts > *lastDecodedPts_) {
        lastDecodedPts_ = pts;
    }
    pendingOutput_.push_back(std::move(data.frame));
    cv_.notify_all();
}

void AudioPipeline::drainDecoderOutput() {
    bool eos = false;
    {
        std::lock_guard lock(mutex_);
        eos = eosReceived_;
    }
    if (!decoder_) {
        return;
    }
    // Drain before deciding on EOS: output produced at the very end must not be lost.
    while (decoder_->hasPendingOutput()) {
        if (decoder_->renderOutput() != Error::Success) {
            break;
        }
    }
    if (eos && !decoder_->hasPendingOutput()) {
        bool signal = false;
        {
            std::lock_guard lock(mutex_);
            if (pendingOutput_.empty() && !decoderEnded_) {
                decoderEnded_ = true;
                signal = true;
            }
        }
        if (signal) {
            sink_->markDecoderEnded();
            if (wake_) {
                wake_();
            }
        }
    }
}

void AudioPipeline::pump() {
    std::lock_guard lock(mutex_);
    bool wrote = false;
    while (!pendingOutput_.empty() && sink_->write(pendingOutput_.front())) {
        pendingOutput_.pop_front();
        wrote = true;
    }
    if (wrote) {
        cv_.notify_all();
    }
    const auto position = sink_->position();
    if (clock_ && position > lastAnchored_) {
        clock_->anchor(position);
        lastAnchored_ = position;
    }
}

std::string AudioPipeline::debugState() const {
    std::lock_guard lock(mutex_);
    return std::format(
        "audio[epoch={} lastPts={:.3f} primed={} position={:.3f} drained={} eos={} ended={}]",
        epoch_, lastDecodedPts_ ? *lastDecodedPts_ : -1.0, sink_->primed(),
        sink_->position().second(), sink_->drained(), eosReceived_, decoderEnded_);
}

void AudioPipeline::fail(slark::Error error) {
    {
        std::lock_guard lock(mutex_);
        if (!failure_) {
            failure_ = error;
        }
    }
    wake();
}

void AudioPipeline::wake() {
    cv_.notify_all();
}

}  // namespace slark::engine
