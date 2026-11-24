//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AudioPipeline.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <utility>

#include "AudioSink.h"
#include "DebugLines.h"
#include "Log.hpp"
#include "MediaClock.h"
#include "Thread.h"

namespace slark::engine {
namespace {

using namespace std::chrono_literals;
// Retry period for a refused frame and for a sink that is full while the pipeline is blocked.
constexpr auto kRetryTick = 20ms;
// While playing, clock anchoring and sink refills follow the buffered audio within this range.
constexpr auto kSinkTickMin = 10ms;
constexpr auto kSinkTickMax = 100ms;
// Refused input without any decoder progress for this long fails the pipeline.
constexpr auto kCodecStallLimit = 2s;

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
    decoder_->setWakeupCallback([gate = wakeGate_, this] {
        AccessGate::Scope access(gate.get());
        if (access) {
            wakeLoop();
        }
    });
    thread_ = LoopThread::create("audioPipeline", [this](LoopThread&) {
        run();
    });
    if (!thread_) {
        fail(Error::OutOfMemory);
    }
}

AudioPipeline::~AudioPipeline() {
    wakeGate_->closeAndWait();
    if (decoder_) {
        decoder_->setWakeupCallback({});
    }
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    wakeLoop();
    if (thread_) {
        thread_->stop();
        thread_->join();
    }
}

void AudioPipeline::flush(SeekEpoch epoch, Time::TimePoint target, SeekMode /*mode*/) {
    {
        std::lock_guard lock(mutex_);
        epoch_ = epoch;
        target_ = target;
        eosReceived_ = false;
        decoderEnded_ = false;
        lastDecodedPts_.reset();
        primedOnce_ = false;
        starved_ = false;
        underrunTicks_ = 0;
        lastAnchored_ = target;
        pendingOutput_.clear();
        written_.clear();
        recording_ = true;
        refusedSince_.reset();
        (void)input_.tryReceiveAll();
        if (sink_) {
            sink_->flush(target);
        }
    }
    if (decoder_) {
        decoder_->flush();
    }
    wakeLoop();
}

void AudioPipeline::retarget(SeekEpoch epoch, Time::TimePoint target, SeekMode /*mode*/) {
    {
        std::lock_guard lock(mutex_);
        epoch_ = epoch;
        target_ = target;
        rebaseLocked(target);
    }
    wakeLoop();
}

// Audio queued for the old target must not play; what lies at or after the new one stays.
void AudioPipeline::rebaseLocked(Time::TimePoint target) {
    std::deque<AVFrameRefPtr> kept;
    for (auto* frames : {&written_, &pendingOutput_}) {
        for (auto& frame : *frames) {
            if (frame->ptsTime() >= target.second()) {
                kept.push_back(std::move(frame));
            }
        }
        frames->clear();
    }
    // Flush even when nothing was written: it also moves the sink's timeline to the new target.
    if (sink_) {
        sink_->flush(target);
    }
    lastDecodedPts_.reset();
    for (const auto& frame : kept) {
        lastDecodedPts_ = std::max(lastDecodedPts_.value_or(frame->ptsTime()), frame->ptsTime());
    }
    pendingOutput_ = std::move(kept);
    decoderEnded_ = false;
    primedOnce_ = false;
    starved_ = false;
    underrunTicks_ = 0;
    lastAnchored_ = target;
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
        if (playing) {
            // A retarget needs a seek in flight, and each seek reopens the record in flush().
            recording_ = false;
            written_.clear();
        }
    }
    if (playing) {
        sink_->start();
    } else {
        sink_->pause();
    }
    wakeLoop();
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
    return primedOnce_ && !starved_;
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
            wakeRequested_ = false;
            if (closed_ || failure_) {
                // A failed pipeline must not keep feeding the dead codec.
                return;
            }
        }
        pullInput();
        drainInbox();
        if (stopping()) {
            return;
        }
        pump();
        drainDecoderOutput();
        {
            std::lock_guard lock(mutex_);
            threadSnapshot_ = ThreadSnapshot{.inboxDepth = inbox_.size(),
                                             .staged = staged_ != nullptr,
                                             .inputClosed = inputClosed_,
                                             .blockedByCodec = blockedByCodec_};
        }
        if (staged_ && !blockedByCodec_ && !outputFull()) {
            // The pump made room for the frame that waited on the sink.
            continue;
        }
        waitForWork();
    }
}

void AudioPipeline::pullInput() {
    for (auto& frame : input_.tryReceiveAll()) {
        inbox_.push_back(std::move(frame));
    }
}

void AudioPipeline::drainInbox() {
    while (!stopping()) {
        if (!staged_) {
            if (inbox_.empty()) {
                return;
            }
            staged_ = std::move(inbox_.front());
            inbox_.pop_front();
        }
        if (!attempt(staged_)) {
            return;
        }
        staged_.reset();
    }
}

bool AudioPipeline::stopping() const {
    std::lock_guard lock(mutex_);
    return closed_ || failure_.has_value();
}

bool AudioPipeline::attempt(AVFrameRefPtr& frame) {
    blockedByCodec_ = false;
    if (!frame) {
        return true;
    }
    if (frame->isEOS) {
        {
            std::lock_guard lock(mutex_);
            // An EOS from before the last flush describes a position nobody is playing from now.
            if (frame->seekEpoch != epoch_) {
                return true;
            }
            eosReceived_ = true;
        }
        if (wake_) {
            wake_();
        }
        return true;
    }
    Time::TimePoint target;
    {
        std::lock_guard lock(mutex_);
        if (frame->seekEpoch != epoch_) {
            return true;
        }
        target = target_;
    }
    // Audio never plays before the target, whatever the seek mode.
    if (!frame->isDiscard && frame->ptsTime() < target.second()) {
        return true;
    }
    if (outputFull()) {
        return false;
    }
    // Platform decoders stamp their output with this epoch; otherwise frames drop as stale.
    decoder_->setCurrentSeekEpoch(frame->seekEpoch);
    const auto error = decoder_->decode(frame);
    if (error == Error::Success) {
        noteProgress();
        return true;
    }
    if (error != Error::CodecAgain && error != Error::CodecNotProcessed) {
        fail(error);
        return true;
    }
    if (refusalExceededLimit()) {
        LogE("audio decoder refused input without progress, pts:{:.3f}", frame->ptsTime());
        fail(Error::CodecResourceExhausted);
        return true;
    }
    blockedByCodec_ = true;
    return false;
}

bool AudioPipeline::outputFull() const {
    std::lock_guard lock(mutex_);
    return pendingOutput_.size() >= kMaxPendingOutput;
}

bool AudioPipeline::refusalExceededLimit() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);
    if (!refusedSince_) {
        refusedSince_ = now;
    }
    return now - *refusedSince_ >= kCodecStallLimit;
}

void AudioPipeline::noteProgress() {
    std::lock_guard lock(mutex_);
    refusedSince_.reset();
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
    {
        std::lock_guard lock(mutex_);
        if (closed_ || data.frame->isEOS || data.frame->isDiscard || epoch != epoch_) {
            return;
        }
        const auto pts = data.frame->ptsTime();
        // Input accepted before a retarget can still come out; it is no longer wanted.
        if (pts < target_.second()) {
            return;
        }
        data.frame->seekEpoch = epoch;
        if (!lastDecodedPts_ || pts > *lastDecodedPts_) {
            lastDecodedPts_ = pts;
        }
        pendingOutput_.push_back(std::move(data.frame));
        refusedSince_.reset();
    }
    wakeLoop();
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
    bool stateChanged = false;
    {
        std::lock_guard lock(mutex_);
        while (!pendingOutput_.empty() && sink_->write(pendingOutput_.front())) {
            if (recording_) {
                written_.push_back(std::move(pendingOutput_.front()));
            }
            pendingOutput_.pop_front();
        }
        const auto position = sink_->position();
        if (clock_ && position > lastAnchored_) {
            clock_->anchor(position);
            lastAnchored_ = position;
        }
        stateChanged = updateUnderrunLocked();
    }
    if (stateChanged && wake_) {
        wake_();
    }
}

// An empty hardware buffer after prefill means the clock keeps running while audio stalls.
// Clearing on pause would let Buffering resolve itself and oscillate; only refilled audio
// (or a finished stream) clears the flag.
bool AudioPipeline::updateUnderrunLocked() {
    const bool wasStarved = starved_;
    const bool finished = eosReceived_ || decoderEnded_ || failure_.has_value();
    if (finished || sink_->primed()) {
        starved_ = false;
        underrunTicks_ = 0;
    } else if (!playing_ || !primedOnce_ || sink_->bufferedSec() > 0.0) {
        // Paused or not yet primed: neither count nor clear, so Buffering cannot self-resolve.
        underrunTicks_ = 0;
    } else if (++underrunTicks_ >= kUnderrunTicks) {
        starved_ = true;
    }
    return wasStarved != starved_;
}

void AudioPipeline::waitForWork() {
    const auto timeout = waitTimeout();
    if (inputClosed_) {
        waitForWake(timeout);
        return;
    }
    auto next = timeout ? input_.receiveFor(*timeout) : input_.receive();
    if (next) {
        inbox_.push_back(std::move(*next));
    } else if (next.error() == ChannelEventType::Closed) {
        inputClosed_ = true;
    }
}

void AudioPipeline::waitForWake(std::optional<std::chrono::milliseconds> timeout) {
    Thread::ScopedInactive inactive;
    std::unique_lock lock(mutex_);
    const auto woken = [this] {
        return wakeRequested_ || closed_;
    };
    if (timeout) {
        cv_.wait_for(lock, *timeout, woken);
    } else {
        cv_.wait(lock, woken);
    }
    wakeRequested_ = false;
}

std::optional<std::chrono::milliseconds> AudioPipeline::waitTimeout() const {
    std::lock_guard lock(mutex_);
    std::optional<std::chrono::milliseconds> timeout;
    if (blockedByCodec_) {
        timeout = kRetryTick;
    }
    // A paused sink does not drain; setPlaying(true) wakes the loop instead of polling.
    if (playing_) {
        const auto tick = sinkTickLocked();
        timeout = timeout ? std::min(*timeout, tick) : tick;
    }
    return timeout;
}

std::chrono::milliseconds AudioPipeline::sinkTickLocked() const {
    const std::chrono::duration<double> half(sink_->bufferedSec() / 2.0);
    return std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(half), kSinkTickMin,
                      kSinkTickMax);
}

DebugInfo AudioPipeline::debugInfo() const {
    DebugInfo result;
    DebugLines queue;
    DebugLines audio;
    DebugLines render;
    std::lock_guard lock(mutex_);

    queue.add("Audio decode queue", threadSnapshot_.inboxDepth, "frames");
    queue.add("Audio pending output", pendingOutput_.size(), "frames");
    queue.add("Audio written", written_.size(), "frames");
    queue.add("Audio buffered", sink_->bufferedSec(), "s");
    queue.add("Audio primed", sink_->primed());
    queue.add("Audio starved", starved_);
    queue.add("Audio drained", sink_->drained());
    queue.add("Blocked by codec", threadSnapshot_.blockedByCodec);
    queue.add("Input closed", threadSnapshot_.inputClosed);
    queue.add("Staged", threadSnapshot_.staged);

    audio.add("Audio position", sink_->position().second(), "s");
    audio.add("Last decoded pts", lastDecodedPts_ ? *lastDecodedPts_ : -1.0, "s");
    audio.add("Decoder EOS", eosReceived_);
    audio.add("Decoder ended", decoderEnded_);
    audio.add("Epoch", epoch_);

    render.add("Decoder", decoder_ ? decoder_->name() : "n/a");
    render.add("Decoder open", decoder_ && decoder_->isOpen());
    render.add("Decoder completed", decoder_ && decoder_->isCompleted());

    result.queue = queue.take();
    result.audio = audio.take();
    result.render = render.take();
    return result;
}

std::string AudioPipeline::debugState() const {
    std::lock_guard lock(mutex_);
    return std::format(
        "audio[epoch={} lastPts={:.3f} primed={} starved={} position={:.3f} drained={} eos={} ended={}]",
        epoch_, lastDecodedPts_ ? *lastDecodedPts_ : -1.0, sink_->primed(), starved_,
        sink_->position().second(), sink_->drained(), eosReceived_, decoderEnded_);
}

void AudioPipeline::fail(slark::Error error) {
    {
        std::lock_guard lock(mutex_);
        if (!failure_) {
            failure_ = error;
        }
    }
    wakeLoop();
}

void AudioPipeline::wakeLoop() {
    {
        std::lock_guard lock(mutex_);
        wakeRequested_ = true;
    }
    cv_.notify_all();
    input_.wakeup();
}

}  // namespace slark::engine
