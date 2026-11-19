//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "VideoPipeline.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <utility>

#include "AVSync.h"
#include "DebugLines.h"
#include "Log.hpp"
#include "MediaClock.h"
#include "Thread.h"
#include "VideoSink.h"

namespace slark::engine {
namespace {

using namespace std::chrono_literals;
// Retry period for a refused frame, undrained codec output, and the catch-up probe.
constexpr auto kRetryTick = 20ms;
// Refused input without any decoder progress for this long fails the pipeline.
constexpr auto kCodecStallLimit = 2s;
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
    decoder_->setWakeupCallback([gate = wakeGate_, this] {
        AccessGate::Scope access(gate.get());
        if (access) {
            wakeLoop();
        }
    });
    thread_ = LoopThread::create("videoPipeline", [this](LoopThread&) {
        run();
    });
    if (!thread_) {
        fail(Error::OutOfMemory);
    }
}

VideoPipeline::~VideoPipeline() {
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
        refusedSince_.reset();
        blockedOnFull_ = false;
        queue_.clear();
        (void)input_.tryReceiveAll();
    }
    if (decoder_) {
        decoder_->flush();
    }
    wakeLoop();
}

void VideoPipeline::retarget(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) {
    {
        std::lock_guard lock(mutex_);
        epoch_ = epoch;
        target_ = target;
        seekMode_ = mode;
    }
    wakeLoop();
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
    bool roomOpened = false;
    {
        std::lock_guard lock(mutex_);
        if (closed_ || failure_) {
            return;
        }
        const bool wasReady = readyLocked();
        const bool wasEnded = endedLocked();
        advanceLocked(clockNow);
        changed = wasReady != readyLocked() || wasEnded != endedLocked();
        // The capacity moves with the sink's held count too, so compare against the blocked state.
        roomOpened = blockedOnFull_ && static_cast<int32_t>(queue_.size()) < queueCapacityLocked();
        if (roomOpened) {
            blockedOnFull_ = false;
        }
    }
    if (changed && wake_) {
        wake_();
    }
    if (roomOpened) {
        wakeLoop();
    }
}

void VideoPipeline::advanceLocked(Time::TimePoint clockNow) {
    // Fast preroll takes the first frame: the source always starts a seek at a keyframe.
    if (!prerolled_) {
        while (!queue_.empty()) {
            const auto& frame = queue_.front();
            if (frame->isDiscard) {
                queue_.pop_front();
                continue;
            }
            const bool qualifies = seekMode_ == SeekMode::Fast ||
                                   frame->ptsTime() >= target_.second() - kPrerollSlackSec;
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
        drainDecoderOutput();
        {
            std::lock_guard lock(mutex_);
            if (catchUpProbe_ && catchUpProbe_()) {
                catchUpToKeyframeLocked();
            }
            threadSnapshot_ = ThreadSnapshot{.inboxDepth = inbox_.size(),
                                     .staged = staged_ != nullptr,
                                     .inputClosed = inputClosed_,
                                     .blockedByCodec = blockedByCodec_,
                                     .outputPending = outputPending_};
        }
        waitForWork();
    }
}

void VideoPipeline::pullInput() {
    for (auto& frame : input_.tryReceiveAll()) {
        inbox_.push_back(std::move(frame));
    }
}

void VideoPipeline::drainInbox() {
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

bool VideoPipeline::stopping() const {
    std::lock_guard lock(mutex_);
    return closed_ || failure_.has_value();
}

bool VideoPipeline::attempt(AVFrameRefPtr& frame) {
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
    SeekMode mode = SeekMode::Exact;
    {
        std::lock_guard lock(mutex_);
        if (frame->seekEpoch != epoch_) {
            return true;
        }
        target = target_;
        mode = seekMode_;
    }
    // Discard marking happens here, against the current target, right before the decoder.
    if (mode == SeekMode::Exact && frame->ptsTime() < target.second() && !frame->isDiscard) {
        frame->isDiscard = true;
    }
    if (queueFull()) {
        // Presentation is the bottleneck: drain, then wait; do not drop the input frame.
        drainDecoderOutput();
        if (queueFull()) {
            std::lock_guard lock(mutex_);
            blockedOnFull_ = true;
            return false;
        }
    }
    decoder_->setCurrentSeekEpoch(frame->seekEpoch);
    const auto error = decoder_->decode(frame);
    if (error == Error::Success) {
        noteProgress();
        // Take the frame out at once: holding it keeps the codec's output buffer checked out.
        drainDecoderOutput();
        return true;
    }
    if (error != Error::CodecAgain && error != Error::CodecNotProcessed) {
        fail(error);
        return true;
    }
    // Input is full because outputs are still checked out; draining lets the codec accept it.
    drainDecoderOutput();
    if (refusalExceededLimit()) {
        LogE("video decoder refused input without progress, pts:{:.3f}", frame->ptsTime());
        fail(Error::CodecResourceExhausted);
        return true;
    }
    blockedByCodec_ = true;
    return false;
}

bool VideoPipeline::queueFull() const {
    std::lock_guard lock(mutex_);
    return static_cast<int32_t>(queue_.size()) >= queueCapacityLocked();
}

bool VideoPipeline::refusalExceededLimit() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);
    if (!refusedSince_) {
        refusedSince_ = now;
    }
    return now - *refusedSince_ >= kCodecStallLimit;
}

void VideoPipeline::noteProgress() {
    std::lock_guard lock(mutex_);
    refusedSince_.reset();
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
    refusedSince_.reset();
}

void VideoPipeline::drainDecoderOutput() {
    if (!decoder_) {
        return;
    }
    // Drain everything the decoder holds: in Texture mode this releases its output buffers.
    bool exhausted = false;
    while (decoder_->hasPendingOutput()) {
        if (decoder_->renderOutput() != Error::Success) {
            // The Android texture decoder keeps its flag set; the next output wakes the loop.
            exhausted = true;
            break;
        }
    }
    outputPending_ = !exhausted && decoder_->hasPendingOutput();
    bool eos = false;
    {
        std::lock_guard lock(mutex_);
        eos = eosReceived_;
    }
    if (eos && !outputPending_) {
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

void VideoPipeline::waitForWork() {
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

void VideoPipeline::waitForWake(std::optional<std::chrono::milliseconds> timeout) {
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

std::optional<std::chrono::milliseconds> VideoPipeline::waitTimeout() const {
    std::lock_guard lock(mutex_);
    // A live catch-up probe is evaluated on every tick; otherwise the queue opens through onVsync.
    if (blockedByCodec_ || outputPending_ || catchUpProbe_) {
        return kRetryTick;
    }
    return std::nullopt;
}

DebugInfo VideoPipeline::debugInfo() const {
    DebugInfo result;
    DebugLines queue;
    DebugLines sync;
    DebugLines render;
    std::lock_guard lock(mutex_);

    queue.add("Video decode queue", threadSnapshot_.inboxDepth, "frames");
    queue.add("Video render queue", queue_.size(), "frames");
    queue.add("Video render queue capacity", queueCapacityLocked(), "frames");
    queue.add("Blocked by codec", threadSnapshot_.blockedByCodec);
    queue.add("Output pending", threadSnapshot_.outputPending);
    queue.add("Input closed", threadSnapshot_.inputClosed);
    queue.add("Staged", threadSnapshot_.staged);

    if (!queue_.empty() && clock_) {
        const auto diffSec = clock_->now().second() - queue_.front()->ptsTime();
        sync.add("A/V sync diff", diffSec * 1000.0, "ms");
        sync.add("Current frame pts", queue_.front()->ptsTime(), "s");
        sync.add("Current frame dts", queue_.front()->dtsTime(), "s");
    } else {
        sync.add("A/V sync diff", "n/a");
    }
    sync.add("Sync threshold", AVSync::kMinThreshold * 1000.0, "ms");
    sync.add("Stalled", starved_);
    sync.add("Stalled vsyncs", starvedVsyncs_);
    sync.add("Decoder EOS", eosReceived_);
    sync.add("Decoder ended", decoderEnded_);
    sync.add("Epoch", epoch_);

    render.add("Presented frame", presentedFrame_);
    render.add("Catch-up count", catchUpCount_);
    render.add("Decoder type", decoder_ ? static_cast<int>(decoder_->type()) : 0);
    render.add("Decoder open", decoder_ && decoder_->isOpen());
    render.add("Decoder completed", decoder_ && decoder_->isCompleted());

    result.queue = queue.take();
    result.sync = sync.take();
    result.render = render.take();
    return result;
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
    wakeLoop();
}

void VideoPipeline::wakeLoop() {
    {
        std::lock_guard lock(mutex_);
        wakeRequested_ = true;
    }
    cv_.notify_all();
    input_.wakeup();
}

}  // namespace slark::engine
