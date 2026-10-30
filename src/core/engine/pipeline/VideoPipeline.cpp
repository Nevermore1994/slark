//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "VideoPipeline.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>

#include <format>

#include "AVSync.h"
#include "Log.hpp"
#include "MediaClock.h"
#include "VideoSink.h"

namespace slark::engine {
namespace {

using namespace std::chrono_literals;
constexpr auto kWakeupTick = 20ms;
// At one tick per try this bounds a wedged codec to about 2s.
constexpr int kMaxDecodeRetries = 100;
constexpr double kPrerollSlackSec = 0.001;  // pts ≥ target − 1ms

[[nodiscard]] bool isPresentableKeyframe(const AVFrameRefPtr& frame) noexcept {
    return frame && frame->info && frame->info->isKeyFrame();
}

}  // namespace

VideoPipeline::VideoPipeline(Receiver<AVFrameRefPtr>& input,
                     DecoderFactory decoderFactory,
                     std::shared_ptr<VideoSink> sink,
                     Config config,
                     SeekEpoch epoch,
                     Time::TimePoint target,
                     std::function<void()> wake)
    : input_(input),
      wake_(std::move(wake)),
      decoderFactory_(std::move(decoderFactory)),
      sink_(std::move(sink)),
      config_(config),
      epoch_(epoch),
      target_(target) {
    // Create before the thread starts so command threads never race the decoder assignment.
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

VideoPipeline::~VideoPipeline() {
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void VideoPipeline::flush(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) {
    {
        std::lock_guard lock(mutex_);
        epoch_ = epoch;
        target_ = target;
        seekMode_ = mode;
        eosReceived_ = false;
        decoderEnded_ = false;
        prerolled_ = false;
        starved_ = false;
        starvedVsyncs_ = 0;
        queue_.clear();
        (void)input_.tryReceiveAll();
    }
    if (decoder_) {
        decoder_->flush();
    }
    cv_.notify_all();
}

void VideoPipeline::retarget(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) {
    std::lock_guard lock(mutex_);
    epoch_ = epoch;
    target_ = target;
    seekMode_ = mode;
}

void VideoPipeline::setPlaying(bool playing) {
    std::lock_guard lock(mutex_);
    if (playing_ == playing) {
        return;
    }
    playing_ = playing;
    if (!playing) {
        // Do not carry a stale starvation verdict into the next resume.
        starved_ = false;
        starvedVsyncs_ = 0;
    }
}

void VideoPipeline::setClock(MediaClock& clock) {
    std::lock_guard lock(mutex_);
    clock_ = &clock;
}

void VideoPipeline::setCatchUpProbe(CatchUpProbe probe) {
    std::lock_guard lock(mutex_);
    catchUpProbe_ = std::move(probe);
}

bool VideoPipeline::endedLocked() const noexcept {
    return eosReceived_ && decoderEnded_ && queue_.empty();
}

// A paused player may have nothing queued; starvation only counts while playing.
bool VideoPipeline::readyLocked() const noexcept {
    return endedLocked() || (prerolled_ && !(playing_ && starved_));
}

bool VideoPipeline::ready(SeekEpoch epoch, Time::TimePoint target) const {
    (void)target;
    std::lock_guard lock(mutex_);
    return epoch == epoch_ && readyLocked();
}

bool VideoPipeline::presentedFrame() const {
    std::lock_guard lock(mutex_);
    return presentedFrame_;
}

bool VideoPipeline::ended() const {
    std::lock_guard lock(mutex_);
    return endedLocked();
}

std::optional<Error> VideoPipeline::failure() const {
    std::lock_guard lock(mutex_);
    return failure_;
}

void VideoPipeline::onVsync(Time::TimePoint clockNow) {
    bool changed = false;
    {
        std::lock_guard lock(mutex_);
        if (closed_ || failure_) {
            return;
        }
        const bool wasReady = readyLocked();
        const bool wasEnded = endedLocked();
        advanceLocked(clockNow);
        changed = wasReady != readyLocked() || wasEnded != endedLocked();
    }
    if (changed && wake_) {
        wake_();
    }
}

void VideoPipeline::advanceLocked(Time::TimePoint clockNow) {

    // Preroll skips the clock: Fast takes the first keyframe, Exact the first frame at the target.
    if (!prerolled_) {
        while (!queue_.empty()) {
            const auto& frame = queue_.front();
            if (frame->isDiscard) {
                queue_.pop_front();
                continue;
            }
            const bool qualifies = seekMode_ == SeekMode::Fast
                                       ? isPresentableKeyframe(frame)
                                       : frame->ptsTime() >= target_.second() - kPrerollSlackSec;
            if (qualifies) {
                sink_->present(frame);
                presentedFrame_ = true;
                queue_.pop_front();
                prerolled_ = true;
                starvedVsyncs_ = 0;
                starved_ = false;
                break;
            }
            queue_.pop_front();
        }
    } else {
        AVSync::Config sync;
        sync.maxDropPerVsync = config_.maxDropPerVsync;
        while (!queue_.empty()) {
            const auto decision = AVSync::decide(clockNow.second(), queue_, sync);
            if (decision.action == AVSync::Action::Hold) {
                break;
            }
            if (decision.action == AVSync::Action::Drop) {
                queue_.erase(queue_.begin(), queue_.begin() + decision.dropCount);
                continue;
            }
            sink_->present(queue_.front());
            queue_.pop_front();
            break;
        }
    }

    // Starvation: enough empty vsyncs while the source is not done clear ready().
    if (!prerolled_ || queue_.empty()) {
        if (!eosReceived_) {
            ++starvedVsyncs_;
            starved_ = starvedVsyncs_ >= config_.starvationThreshold;
        }
    } else {
        starvedVsyncs_ = 0;
        starved_ = false;
    }
}

void VideoPipeline::run() {
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
        {
            std::lock_guard lock(mutex_);
            if (catchUpProbe_ && catchUpProbe_()) {
                catchUpToKeyframeLocked();
            }
        }
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, kWakeupTick);
    }
}

void VideoPipeline::handleInput(AVFrameRefPtr& frame) {
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
    Time::TimePoint target;
    SeekMode mode = SeekMode::Exact;
    {
        std::lock_guard lock(mutex_);
        if (frame->seekEpoch != epoch_) {
            return;
        }
        target = target_;
        mode = seekMode_;
    }
    // Discard marking happens here, against the current target, right before the decoder.
    if (mode == SeekMode::Exact && frame->ptsTime() < target.second() && !frame->isDiscard) {
        frame->isDiscard = true;
    }
    if (!decodeOne(frame)) {
        return;
    }
    drainDecoderOutput();
}

bool VideoPipeline::decodeOne(AVFrameRefPtr& frame) {
    // A codec that refuses input without producing output is dead: fail instead of waiting.
    int againRetries = 0;
    while (true) {
        {
            std::unique_lock lock(mutex_);
            if (closed_) {
                return false;
            }
            if (frame->seekEpoch != epoch_) {
                // A flush happened while this frame was waiting for room.
                return false;
            }
            if (static_cast<int32_t>(queue_.size()) >= queueCapacityLocked()) {
                // Presentation is the bottleneck: drain, then wait; do not drop the input frame.
                lock.unlock();
                drainDecoderOutput();
                lock.lock();
                if (closed_) {
                    return false;
                }
                if (static_cast<int32_t>(queue_.size()) >= queueCapacityLocked()) {
                    cv_.wait_for(lock, kWakeupTick);
                }
                continue;
            }
        }
        // Platform decoders stamp their output with this epoch; otherwise frames drop as stale.
        decoder_->setCurrentSeekEpoch(frame->seekEpoch);
        const auto error = decoder_->decode(frame);
        if (error == Error::Success) {
            // Take the frame out at once: holding it keeps the codec's output buffer checked out.
            drainDecoderOutput();
            return true;
        }
        if (error != Error::CodecAgain && error != Error::CodecNotProcessed) {
            fail(error);
            return false;
        }
        // Input is full because outputs are still checked out; draining lets the codec accept it.
        drainDecoderOutput();
        if (++againRetries > kMaxDecodeRetries) {
            LogE("video decode retry exhausted after {}, pts:{:.3f}", againRetries,
                 frame->ptsTime());
            fail(Error::CodecResourceExhausted);
            return false;
        }
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, kWakeupTick);
    }
}

void VideoPipeline::handleDecoded(slark::DecoderData data) {
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
    queue_.push_back(std::move(data.frame));
    cv_.notify_all();
}

void VideoPipeline::drainDecoderOutput() {
    if (!decoder_) {
        return;
    }
    // Drain everything the decoder holds: in Texture mode this releases its output buffers.
    while (decoder_->hasPendingOutput()) {
        if (decoder_->renderOutput() != Error::Success) {
            break;
        }
    }
    bool eos = false;
    {
        std::lock_guard lock(mutex_);
        eos = eosReceived_;
    }
    if (eos && !decoder_->hasPendingOutput()) {
        std::lock_guard lock(mutex_);
        if (!decoderEnded_) {
            decoderEnded_ = true;
            if (wake_) {
                wake_();
            }
        }
    }
}

void VideoPipeline::catchUpToKeyframeLocked() {
    while (!queue_.empty() && !isPresentableKeyframe(queue_.front())) {
        queue_.pop_front();
    }
    prerolled_ = false;
    ++catchUpCount_;
}

std::string VideoPipeline::debugState() const {
    std::lock_guard lock(mutex_);
    return std::format(
        "video[epoch={} prerolled={} starved={} queue={} starvedVsyncs={} eos={} ended={} "
        "target={:.2f}]",
        epoch_, prerolled_, starved_, queue_.size(), starvedVsyncs_, eosReceived_, decoderEnded_,
        target_.second());
}

std::uint64_t VideoPipeline::catchUpCount() const {
    std::lock_guard lock(mutex_);
    return catchUpCount_;
}

int32_t VideoPipeline::queueCapacityLocked() const {
    return config_.maxQueueFrames + sink_->heldCount();
}

void VideoPipeline::fail(slark::Error error) {
    {
        std::lock_guard lock(mutex_);
        if (!failure_) {
            failure_ = error;
        }
    }
    wake();
}

void VideoPipeline::wake() {
    cv_.notify_all();
}

}  // namespace slark::engine
