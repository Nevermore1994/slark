//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <type_traits>
#include <utility>

#include "Log.hpp"

namespace slark::engine {
namespace {

using namespace std::chrono_literals;

constexpr Scheduler::Slot kWakeSlot = 1;
constexpr int kMaxSettlePasses = 4;
constexpr int64_t kNearSeekUs = 50'000;
constexpr auto kPositionTick = 200ms;
constexpr auto kLoadingTick = 500ms;
constexpr auto kMinWakeGap = 1ms;
// A seek that takes longer than this is considered stalled: retry it, then fail.
constexpr auto kSeekStallTimeout = 3s;  // in seconds when compared via .second()
constexpr int kMaxSeekAttempts = 3;

}  // namespace

std::shared_ptr<Engine> Engine::create(Scheduler& scheduler,
                                       Notifier& notifier,
                                       PlaybackFactory factory) {
    return std::shared_ptr<Engine>(new Engine(scheduler, notifier, std::move(factory)));
}

Engine::Engine(Scheduler& scheduler, Notifier& notifier, PlaybackFactory factory)
    : scheduler_(scheduler), notifier_(notifier), factory_(std::move(factory)), clock_(scheduler) {}

void Engine::post(Command command) {
    scheduler_.execute([weak = weak_from_this(), command = std::move(command)]() mutable {
        if (auto self = weak.lock()) {
            if (self->closed_.load(std::memory_order_acquire)) {
                return;
            }
            self->apply(command);
            self->reconcile();
        }
    });
}

void Engine::close() noexcept {
    closed_.store(true, std::memory_order_release);
}

Snapshot Engine::snapshot() const {
    std::lock_guard lock(snapshotMutex_);
    return snapshot_;
}

PlaybackFactory::Wake Engine::wakeFn() {
    return [weak = weak_from_this()] {
        if (auto self = weak.lock()) {
            self->wake();
        }
    };
}

void Engine::wake() {
    if (wakePending_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    scheduler_.execute([weak = weak_from_this()] {
        if (auto self = weak.lock()) {
            if (self->closed_.load(std::memory_order_acquire)) {
                return;
            }
            self->wakePending_.store(false, std::memory_order_release);
            self->reconcile();
        }
    });
}

void Engine::apply(Command& command) {
    if (released_ && !std::holds_alternative<Release>(command)) {
        reject();
        return;
    }
    std::visit(
        [this](auto& c) {
            using T = std::remove_cvref_t<decltype(c)>;
            if constexpr (std::is_same_v<T, Prepare>) {
                onPrepare(c);
            } else if constexpr (std::is_same_v<T, Play>) {
                onPlay();
            } else if constexpr (std::is_same_v<T, Pause>) {
                onPause();
            } else if constexpr (std::is_same_v<T, Seek>) {
                onSeek(c);
            } else if constexpr (std::is_same_v<T, SetSpeed>) {
                onSetSpeed(c);
            } else if constexpr (std::is_same_v<T, SetRepeat>) {
                onSetRepeat(c);
            } else if constexpr (std::is_same_v<T, SetVolume>) {
                onSetVolume(c);
            } else if constexpr (std::is_same_v<T, SetMute>) {
                onSetMute(c);
            } else if constexpr (std::is_same_v<T, Stop>) {
                onStop();
            } else if constexpr (std::is_same_v<T, Release>) {
                onRelease();
            } else {
                static_assert(false, "unhandled command");
            }
        },
        command);
}

void Engine::reject() {
    notifier_.notify(ErrorEvent{Error::InvalidOperation, false});
}

bool Engine::mayControl() {
    if (state_.failure) {
        reject();
        return false;
    }
    return true;
}

void Engine::fail(Error error, ErrorDetail detail) {
    if (state_.failure) {
        return;
    }
    state_.failure = error;
    notifier_.notify(ErrorEvent{error, true, std::move(detail)});
}

void Engine::onPrepare(Prepare& c) {
    if (state_.hasSession || state_.failure) {
        reject();
        return;
    }
    state_.hasSession = true;
    state_.volume = c.setting.volume / 100.0f;
    state_.repeat = c.setting.isLoop ? RepeatMode::One : RepeatMode::Off;
    bufferPolicy_ = BufferPolicy(c.setting.flowConfig.value_or(FlowConfig{}));
    readyEpoch_ = ++epochCounter_;
    readyTarget_ = Time::TimePoint{};
    source_ = factory_.createSource(c, readyEpoch_, wakeFn());
    if (!source_) {
        fail(Error::SchemeNotSupported);
        return;
    }
    if (c.startPos.point() > 0) {
        requestSeek(c.startPos, SeekMode::Exact, true);
    }
}

void Engine::onPlay() {
    if (!mayControl()) {
        return;
    }
    if (deriveStatus(state_, evaluatePipelines()) == Status::Ended) {
        requestSeek({}, SeekMode::Exact, true);
    }
    state_.wantPlay = true;
}

void Engine::onPause() {
    if (mayControl()) {
        state_.wantPlay = false;
    }
}

void Engine::onSeek(Seek& c) {
    if (!mayControl()) {
        return;
    }
    if (!state_.hasSession || (state_.info.tracksKnown && !state_.info.isSeekable)) {
        reject();
        return;
    }
    auto target = c.target;
    if (state_.info.duration.point() > 0) {
        target = std::min(target, state_.info.duration);
    }
    const bool inFlight = state_.seekTarget && state_.issuedEpoch != kInvalidSeekEpoch;
    if (inFlight && source_ && source_->retarget(target)) {
        retargetSeek(target, c.mode);
        return;
    }
    const bool near = std::abs((target - clock_.now()).point()) <= kNearSeekUs;
    if (state_.info.tracksKnown && !state_.seekTarget && near) {
        notifier_.notify(SeekDone{});
        return;
    }
    requestSeek(target, c.mode, false);
}

void Engine::onSetSpeed(SetSpeed& c) {
    if (c.speed <= 0.0) {
        reject();
        return;
    }
    state_.speed = c.speed;
    clock_.setRate(c.speed);
}

void Engine::onSetRepeat(SetRepeat& c) {
    state_.repeat = c.mode;
    LogI("repeat mode: {}", c.mode == RepeatMode::One ? "one" : "off");
}

void Engine::onSetVolume(SetVolume& c) {
    state_.volume = std::clamp(c.volume, 0.0f, 1.0f);
    applyVolume();
}

void Engine::onSetMute(SetMute& c) {
    state_.muted = c.muted;
    applyVolume();
}

void Engine::applyVolume() {
    const float effective = state_.muted ? 0.0f : state_.volume;
    for (auto& pipeline : pipelines_) {
        pipeline->setVolume(effective);
    }
}

void Engine::onStop() {
    teardown();
}

void Engine::onRelease() {
    teardown();
    released_ = true;
}

void Engine::requestSeek(Time::TimePoint target, SeekMode mode, bool silent) {
    state_.seekTarget = target;
    state_.seekMode = mode;
    state_.issuedEpoch = kInvalidSeekEpoch;
    silentSeek_ = silent;
}

void Engine::retargetSeek(Time::TimePoint target, SeekMode mode) {
    state_.seekTarget = target;
    state_.seekMode = mode;
    readyTarget_ = target;
    silentSeek_ = false;
    LogI("seek retarget: target={:.3f}s mode={} epoch={}", target.second(),
         mode == SeekMode::Fast ? "fast" : "exact", readyEpoch_);
    clock_.seekTo(target);
    for (auto& pipeline : pipelines_) {
        pipeline->retarget(readyEpoch_, target, mode);
    }
}

// vsync entry from the render side: sync the video pipeline with the current media position.
void Engine::notifyVsync() {
    std::lock_guard lock(pipelinesMutex_);
    if (pipelines_.empty()) {
        return;
    }
    const auto now = clock_.running() ? clock_.now() : readyTarget_.load();
    for (auto& pipeline : pipelines_) {
        pipeline->onVsync(now);
    }
}

void Engine::releasePipelines() {
    std::vector<std::unique_ptr<Pipeline>> released;
    {
        std::lock_guard lock(pipelinesMutex_);
        released.swap(pipelines_);
    }
}

void Engine::teardown() {
    if (source_) {
        source_->setSeeking(false);
    }
    scheduler_.cancel(kWakeSlot);
    releasePipelines();
    if (source_) {
        source_->close();
        source_.reset();
    }
    clock_.reset();
    state_ = EngineState{};
    readyEpoch_ = kInvalidSeekEpoch;
    readyTarget_ = Time::TimePoint{};
    started_ = false;
    rebuffering_ = false;
    silentSeek_ = false;
    firstFrameNotified_ = false;
}

bool Engine::adoptSession() {
    if (!source_) {
        return false;
    }
    bool changed = false;
    if (const auto info = source_->info(); !(info == state_.info)) {
        state_.info = info;
        changed = true;
    }
    if (!state_.failure) {
        if (const auto failure = source_->failure()) {
            fail(*failure, source_->failureDetail());
            changed = true;
        }
    }
    if (!state_.failure && state_.info.tracksKnown && pipelines_.empty()) {
        auto created = factory_.createPipelines(*source_, state_.info, readyEpoch_, wakeFn());
        for (auto& pipeline : created) {
            pipeline->setClock(clock_);
            pipeline->setVolume(state_.muted ? 0.0f : state_.volume);
        }
        {
            std::lock_guard lock(pipelinesMutex_);
            pipelines_ = std::move(created);
        }
        if (pipelines_.empty()) {
            fail(Error::PipelineFormatNotSupported);
        }
        changed = true;
    }
    for (const auto& pipeline : pipelines_) {
        if (state_.failure) {
            break;
        }
        if (const auto failure = pipeline->failure()) {
            fail(*failure);
            changed = true;
        }
    }
    return changed;
}

bool Engine::startSeek() {
    if (!state_.seekTarget || !state_.info.tracksKnown || state_.issuedEpoch != kInvalidSeekEpoch ||
        !source_ || state_.failure) {
        return false;
    }
    const auto target = *state_.seekTarget;
    state_.issuedEpoch = ++epochCounter_;
    readyEpoch_ = state_.issuedEpoch;
    readyTarget_ = target;
    started_ = false;
    rebuffering_ = false;
    clock_.stop();
    clock_.seekTo(target);
    seekStartedAt_ = scheduler_.now();
    LogI("seek start: target={:.3f}s mode={} epoch={}", target.second(),
         state_.seekMode == SeekMode::Fast ? "fast" : "exact", readyEpoch_);
    source_->setSeeking(true);
    // Pipelines must hold the new epoch before the source publishes it, or its keyframe is dropped as stale.
    for (auto& pipeline : pipelines_) {
        pipeline->flush(readyEpoch_, target, state_.seekMode);
    }
    source_->seekTo(target, readyEpoch_);
    return true;
}

bool Engine::retryStuckSeek() {
    if (!state_.seekTarget || state_.issuedEpoch == kInvalidSeekEpoch || !source_) {
        return false;
    }
    if ((scheduler_.now() - seekStartedAt_).second() < kSeekStallTimeout.count()) {
        return false;
    }
    if (seekAttempts_ >= kMaxSeekAttempts) {
        LogE("seek did not complete after {} attempts, failing", seekAttempts_);
        source_->setSeeking(false);
        fail(Error::PipelineDemuxError);
        return true;
    }
    ++seekAttempts_;
    const auto target = *state_.seekTarget;
    state_.issuedEpoch = ++epochCounter_;
    readyEpoch_ = state_.issuedEpoch;
    readyTarget_ = target;
    started_ = false;
    seekStartedAt_ = scheduler_.now();
    LogW("seek stalled at {:.3f}s, retry {}", target.second(), seekAttempts_);
    clock_.stop();
    clock_.seekTo(target);
    for (auto& pipeline : pipelines_) {
        pipeline->flush(readyEpoch_, target, state_.seekMode);
    }
    source_->seekTo(target, readyEpoch_);
    return true;
}

bool Engine::completeSeek() {
    if (!state_.seekTarget || state_.issuedEpoch == kInvalidSeekEpoch) {
        return false;
    }
    const bool ready = std::ranges::all_of(pipelines_, [this](const auto& pipeline) {
        return !pipeline->blocksReady() || pipeline->ready(readyEpoch_, readyTarget_);
    });
    if (!ready) {
        return false;
    }
    LogI("seek done: epoch={} after {:.3f}s retries={}", readyEpoch_,
         (scheduler_.now() - seekStartedAt_).second(), seekAttempts_);
    state_.seekTarget.reset();
    state_.issuedEpoch = kInvalidSeekEpoch;
    seekAttempts_ = 0;
    if (source_) {
        source_->setSeeking(false);
    }
    if (!silentSeek_) {
        notifier_.notify(SeekDone{});
    }
    silentSeek_ = false;
    return true;
}

bool Engine::repeatIfEnded() {
    if (state_.repeat == RepeatMode::Off || state_.seekTarget || pipelines_.empty()) {
        return false;
    }
    if (!std::ranges::all_of(pipelines_, &Pipeline::ended)) {
        if (std::ranges::any_of(pipelines_, &Pipeline::ended)) {
            std::string states;
            for (const auto& pipeline : pipelines_) {
                states += pipeline->debugState() + ' ';
            }
            LogIThrottle(1000, "repeat waits for the other pipeline: {}", states);
        }
        return false;
    }
    LogI("repeat: restarting from 0, epoch={}", readyEpoch_);
    notifier_.notify(PlayEnd{});
    requestSeek({}, SeekMode::Exact, true);
    return true;
}

PipelineView Engine::evaluatePipelines() {
    PipelineView view;
    view.blockingNotReady = std::ranges::any_of(pipelines_, [this](const auto& pipeline) {
        return pipeline->blocksReady() && !pipeline->ready(readyEpoch_, readyTarget_);
    });
    view.allEnded = !pipelines_.empty() && std::ranges::all_of(pipelines_, &Pipeline::ended);

    if (!source_ || !source_->isLoading()) {
        rebuffering_ = false;
        return view;
    }
    const double ahead = (source_->cacheEnd() - clock_.now()).second();
    if (started_) {
        rebuffering_ = bufferPolicy_.shouldRebuffer(ahead, rebuffering_);
        view.aheadBelowStart = rebuffering_;
    } else {
        view.aheadBelowStart = !bufferPolicy_.canStart(ahead);
    }
    return view;
}

void Engine::applyClock() {
    const bool playing = status_ == Status::Ready && state_.wantPlay;
    if (playing) {
        clock_.start();
    } else {
        clock_.stop();
    }
    for (auto& pipeline : pipelines_) {
        pipeline->setPlaying(playing);
    }
}

void Engine::reconcile() {
    for (int pass = 0; pass < kMaxSettlePasses; ++pass) {
        bool changed = adoptSession();
        changed = startSeek() || changed;
        changed = completeSeek() || changed;
        changed = retryStuckSeek() || changed;
        changed = repeatIfEnded() || changed;
        if (!changed) {
            break;
        }
    }

    const auto previousStatus = status_;
    status_ = deriveStatus(state_, evaluatePipelines());
    if (status_ == Status::Ready) {
        started_ = true;
    }
    if (status_ == Status::Ended && previousStatus != Status::Ended) {
        notifier_.notify(PlayEnd{});
    }
    if (!firstFrameNotified_ && std::ranges::any_of(pipelines_, &Pipeline::presentedFrame)) {
        firstFrameNotified_ = true;
        notifier_.notify(FirstFrameRendered{});
    }
    applyClock();
    if ((status_ == Status::Ended || status_ == Status::Failed) && state_.wantPlay) {
        state_.wantPlay = false;
    }

    if (source_) {
        source_->regulate(clock_.now());
    }
    publish();
    scheduleWakeup();
}

void Engine::publish() {
    if (status_ == Status::Buffering || status_ == Status::Seeking) {
        std::string pipelines;
        for (const auto& pipeline : pipelines_) {
            pipelines += pipeline->debugState();
            pipelines += ' ';
        }
        LogIThrottle(1000,
                     "{}: wantPlay={} started={} clock={:.3f} cacheEnd={:.3f} "
                     "epoch={} | {}",
                     status_ == Status::Seeking      ? "seeking"
                     : status_ == Status::Buffering ? "buffering"
                     : status_ == Status::Ready     ? "ready"
                                                    : "other",
                     state_.wantPlay,
                     started_, clock_.now().second(),
                     source_ ? source_->cacheEnd().second() : -1.0, readyEpoch_, pipelines);
    }
    Snapshot s;
    s.status = status_;
    s.wantPlay = state_.wantPlay;
    s.isPlaying = isPlaying(state_, status_);
    s.isLoading = source_ && source_->isLoading();
    s.isSeekable = state_.info.tracksKnown && state_.info.isSeekable;
    s.muted = state_.muted;
    s.hasAudio = state_.info.hasAudio;
    s.hasVideo = state_.info.hasVideo;
    s.tracksKnown = state_.info.tracksKnown && !pipelines_.empty();
    s.duration = state_.info.duration;
    s.position = status_ == Status::Ended ? s.duration : clock_.now();
    s.buffered = source_ ? source_->cacheEnd() : Time::TimePoint{};
    if (s.duration.point() > 0) {
        s.buffered = std::min(s.buffered, s.duration);
    }
    s.speed = state_.speed;
    s.volume = state_.volume;
    {
        std::lock_guard lock(snapshotMutex_);
        snapshot_ = s;
    }
    notifier_.publish(s, scheduler_.now());
}

void Engine::scheduleWakeup() {
    const auto now = scheduler_.now();
    std::optional<Time::TimePoint> at;
    const auto consider = [&](Time::TimePoint t) {
        at = at ? std::min(*at, t) : t;
    };
    for (const auto& pipeline : pipelines_) {
        if (const auto next = pipeline->nextWakeup()) {
            consider(*next);
        }
    }
    if (clock_.running()) {
        consider(now + kPositionTick);
    } else if (status_ == Status::Opening || status_ == Status::Buffering ||
               status_ == Status::Seeking) {
        // These states progress without a running clock, so keep ticking.
        consider(now + kLoadingTick);
    }
    if (source_ && source_->isLoading()) {
        consider(now + kLoadingTick);
    }
    if (const auto due = notifier_.cacheReportDue()) {
        consider(*due);
    }
    if (!at) {
        scheduler_.cancel(kWakeSlot);
        return;
    }
    scheduler_.postAt(kWakeSlot, std::max(*at, now + kMinWakeGap), [weak = weak_from_this()] {
        if (auto self = weak.lock()) {
            if (self->closed_.load(std::memory_order_acquire)) {
                return;
            }
            self->reconcile();
        }
    });
}

}  // namespace slark::engine
