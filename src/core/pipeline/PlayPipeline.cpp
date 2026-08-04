//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayPipeline.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <map>

#include "AudioRenderComponent.h"
#include "BufferMonitor.h"
#include "DecoderComponent.h"
#include "DemuxerComponent.h"
#include "Error.h"
#include "IDemuxer.h"
#include "Log.hpp"
#include "Thread.h"
#include "perf/Perf.h"

namespace slark {

namespace {
constexpr auto kTeardownTimeout = std::chrono::milliseconds(2000);
}  // namespace

PlayPipeline::PlayPipeline(BackpressureConfig config) noexcept : config_(std::move(config)) {
    assert(config_.isValid() && "BackpressureConfig is invalid");
    clockSync_ = std::make_unique<ClockSync>();
    seekContext_ = std::make_unique<SeekContext>();
    bufferMonitor_ = std::make_unique<BufferMonitor>(config_, *this);
    connectComponents();
    pipelineState_.store(PipelineState::Built, std::memory_order_release);
    LogI("PlayPipeline built");
}

PlayPipeline::~PlayPipeline() {
    const auto s = pipelineState_.load(std::memory_order_acquire);
    if (s != PipelineState::Idle && s != PipelineState::TearingDown) {
        teardown();
    }
}

void PlayPipeline::build() noexcept {
    assert(isOnOwnerThread());
    if (componentStarted_.exchange(true)) {
        return;
    }
    if (demuxer_) {
        demuxer_->start();
    }
    if (!videoRender_.load()) {
        LogW("PlayPipeline::build: videoRender is null");
    }
    LogI("PlayPipeline build complete");
}

void PlayPipeline::start() noexcept {
    assert(isOnOwnerThread());
    const auto cur = pipelineState_.load(std::memory_order_acquire);
    LogI("PlayPipeline start, current state:{}", static_cast<int>(cur));
    if (cur == PipelineState::Faulted) {
        fireEvent(PipelineEvent::ReadError, PipelineError{.error = Error::InvalidOperation});
        return;
    }
    if (cur == PipelineState::Playing) {
        return;
    }

    if (cur == PipelineState::Paused || cur == PipelineState::Built) {
        if (demuxer_) {
            demuxer_->start();
        }
        if (audioRender_) {
            audioRender_->start();
        }
        if (auto render = videoRender_.load()) {
            render->start();
        }
    }
    pipelineState_.store(PipelineState::Playing, std::memory_order_release);
    clockSync_->setClockRunning(true);
}

void PlayPipeline::pause() noexcept {
    assert(isOnOwnerThread());
    const auto s = pipelineState_.load(std::memory_order_acquire);
    if (s == PipelineState::Faulted) {
        fireEvent(PipelineEvent::ReadError, PipelineError{.error = Error::InvalidOperation});
        return;
    }
    if (s == PipelineState::Paused) {
        return;
    }
    if (audioRender_) {
        audioRender_->pause();
    }
    if (auto render = videoRender_.load()) {
        render->pause();
    }
    pipelineState_.store(PipelineState::Paused, std::memory_order_release);
    clockSync_->setClockRunning(false);
}

void PlayPipeline::flush() noexcept {
    assert(isOnOwnerThread());
    if (pipelineState_.load(std::memory_order_acquire) == PipelineState::Faulted) {
        fireEvent(PipelineEvent::ReadError, PipelineError{.error = Error::InvalidOperation});
        return;
    }

    videoFrames_.withLock([](auto& m) {
        m.clear();
    });
    audioFrames_.withLock([](auto& q) {
        q.clear();
    });

    readiness_.liveVideoKeyframeSeen = false;
    readiness_.liveAudioPacketSeen = false;

    lastEnqueuedAudioPts_.store(0.0, std::memory_order_release);
    lastEnqueuedVideoPts_.store(0.0, std::memory_order_release);
    eosInjected_.store(false, std::memory_order_release);

    if (bufferMonitor_) {
        bufferMonitor_->reset();
    }
    if (demuxer_) {
        demuxer_->flush();
    }
    if (audioDecoder_) {
        audioDecoder_->flush();
    }
    if (videoDecoder_) {
        videoDecoder_->flush();
    }
    if (audioRender_) {
        audioRender_->seek(0.0);
    }
    if (auto render = videoRender_.load()) {
        render->flush();
    }
}

void PlayPipeline::teardown() noexcept {
    const auto cur = pipelineState_.load(std::memory_order_acquire);
    if (cur == PipelineState::Idle || cur == PipelineState::TearingDown) {
        return;
    }
    pipelineState_.store(PipelineState::TearingDown, std::memory_order_release);
    LogI("PlayPipeline teardown");
    stopComponents();
    bufferMonitor_.reset();
    const bool joined = joinWithTimeout(kTeardownTimeout);
    if (!joined) {
        LogE("PlayPipeline teardown timeout, entering Faulted");
        enterFaultedState();
    }
    // 不论是否 timeout，teardown 都已"尽力而为"，最终置 Idle 让 owner 可以安全释放，
    // 避免上层反复重入 teardown 触发死循环。Faulted 仅作为错误事件上报副作用。
    pipelineState_.store(PipelineState::Idle, std::memory_order_release);
}

void PlayPipeline::applyLiveConfig() noexcept {
    assert(isOnOwnerThread());
    if (config_.isLiveSource) {
        return;
    }
    config_ = BackpressureConfig::forLive();
    bufferMonitor_ = std::make_unique<BufferMonitor>(config_, *this);
    LogI("PlayPipeline: switched to live backpressure config");
}

SeekStrategy PlayPipeline::resolveSeekStrategy(double targetSec) const noexcept {
    bool canFastForward = false;
    videoFrames_.withLock([targetSec, &canFastForward](const auto& m) {
        if (!m.empty()) {
            canFastForward = m.begin()->second->ptsTime() <= targetSec &&
                             m.rbegin()->second->ptsTime() >= targetSec;
        }
    });
    const double currentSec = clockSync_->masterTime().second();
    auto strategy = canFastForward ? SeekStrategy::FastForwardInGOP : SeekStrategy::SeekToKeyframe;
    LogI("resolveSeekStrategy, current={:.3f} target={:.3f} canFastForward={} -> strategy={}",
         currentSec, targetSec, canFastForward, static_cast<int32_t>(strategy));
    return strategy;
}

void PlayPipeline::seek(CTime target, bool isAccurate) noexcept {
    assert(isOnOwnerThread());
    if (pipelineState_.load(std::memory_order_acquire) == PipelineState::Faulted) {
        fireEvent(PipelineEvent::ReadError, PipelineError{.error = Error::InvalidOperation});
        return;
    }

    const double targetSec = target.second();
    const double currentSec = clockSync_->masterTime().second();

    constexpr double kMinSeekDistance = 0.05;
    if (std::abs(targetSec - currentSec) <= kMinSeekDistance) {
        std::ignore = seekContext_->beginSeek(target, resolveSeekStrategy(targetSec));
        std::ignore = seekContext_->completeSeek();
        postToOwnerThread([this] {
            fireEvent(PipelineEvent::SeekCompleted);
        });
        LogI("seek distance too small, skip seek and notify completed: target={:.3f}s, "
             "current={:.3f}s",
             targetSec, currentSec);
        return;
    }

    if (seekContext_->isSeekPending() && !isAccurate) {
        LogI("seek pending, ignored: target={:.3f}s, isAccurate={}", targetSec, isAccurate);
        return;
    }

    const SeekStrategy strategy = resolveSeekStrategy(targetSec);
    [[maybe_unused]] const bool seekStarted = seekContext_->beginSeek(target, strategy);
    {
        double fps = clockSync_->videoFps();
        int32_t maxSkip =
            (fps > 0.0)
                ? std::max(1, static_cast<int32_t>(
                                  fps * static_cast<double>(config_.seekFastForwardMs) / 1000.0))
                : 1;
        seekContext_->setMaxSkipFrames(maxSkip);
    }
    SLARK_TRACE_MILESTONE(playSession_.lock().get(), "seek_start");

    if (bufferMonitor_) {
        bufferMonitor_->pauseBackpressure();
    }

    if (strategy == SeekStrategy::FastForwardInGOP) {
        fastForwardInGOP(target);
    } else {
        performKeyframeSeek(target, strategy);
    }
}

void PlayPipeline::fastForwardInGOP(CTime target) noexcept {
    assert(isOnOwnerThread());
    const double targetSec = target.second();

    bool audioBufferLanded = false;
    audioFrames_.withLock([targetSec, &audioBufferLanded](auto& q) {
        while (!q.empty() && (!q.front() || q.front()->ptsTime() < targetSec)) {
            q.pop_front();
        }
        audioBufferLanded = !q.empty();
    });
    if (audioRender_) {
        audioRender_->seek(targetSec);
    }

    clockSync_->seekTo(Time::TimePoint::fromSeconds(targetSec));

    if (demuxer_) {
        demuxer_->resume();
    }

    LogI("seek fast-forward in GOP to {:.3f}s, epoch={}", targetSec, seekContext_->currentEpoch());

    if (audioBufferLanded && mediaInfo_.hasAudio && !seekContext_->isAudioSeeked()) {
        onSeekFrameConsumed(TrackType::Audio);
    }
}

void PlayPipeline::performKeyframeSeek(CTime target, SeekStrategy strategy) noexcept {
    assert(isOnOwnerThread());
    const double targetSec = target.second();
    LogI("performKeyframeSeek ENTER target={:.3f} strategy={} epoch={}", targetSec,
         static_cast<int32_t>(strategy), seekContext_->currentEpoch());
    pause();
    if (demuxer_) {
        demuxer_->pause();
    }
    if (audioDecoder_) {
        audioDecoder_->pause();
    }
    if (videoDecoder_) {
        videoDecoder_->pause();
    }
    pauseStream();
    flush();
    if (audioRender_) {
        audioRender_->seek(targetSec);
    }
    clockSync_->seekTo(Time::TimePoint::fromSeconds(targetSec));
    if (mediaInfo_.streamType == StreamType::HLS) {
        if (seekByTimeFunc_) {
            auto res = seekByTimeFunc_(targetSec);
            if (!res.has_value()) {
                LogE("HLS seekByTime failed: {}", static_cast<uint32_t>(res.error()));
            }
        }
    } else if (demuxer_) {
        auto pos = demuxer_->getSeekToPos(targetSec);
        LogI("performKeyframeSeek demuxer relocate seekPos={:.3f}", targetSec);
        demuxer_->seekToPos(pos);
        demuxer_->resume();
        LogI("performKeyframeSeek demuxer resumed");
    }
    seekContext_->updateGOPRange(-1.0, -1.0);
    if (audioDecoder_) {
        audioDecoder_->resume();
    }
    if (videoDecoder_) {
        videoDecoder_->resume();
    }
    resumeStream();
    LogI("seek to keyframe for {:.3f}s (strategy={}), epoch={}", targetSec,
         static_cast<int32_t>(strategy), seekContext_->currentEpoch());
}

Time::TimePoint PlayPipeline::currentTime() noexcept {
    return clockSync_->masterTime();
}

Time::TimePoint PlayPipeline::audioTime() noexcept {
    return clockSync_->audioTime();
}

double PlayPipeline::cachedDuration() const noexcept {
    return demuxedDuration();
}

double PlayPipeline::maxBufferedPts() const noexcept {
    double maxPts = 0.0;
    videoFrames_.withLock([&maxPts](const auto& m) {
        if (!m.empty()) {
            maxPts = std::max(maxPts, m.rbegin()->second->ptsTime());
        }
    });
    audioFrames_.withLock([&maxPts](const auto& q) {
        if (!q.empty()) {
            maxPts = std::max(maxPts, q.back()->ptsTime());
        }
    });
    maxPts = std::max(maxPts, lastEnqueuedAudioPts_.load(std::memory_order_acquire));
    maxPts = std::max(maxPts, lastEnqueuedVideoPts_.load(std::memory_order_acquire));
    return maxPts;
}

double PlayPipeline::cacheEndTime() const noexcept {
    return std::max(0.0, maxBufferedPts());
}

bool PlayPipeline::isDecodeCompleted() const noexcept {
    if (audioDecoder_) {
        auto dec = audioDecoder_->decoder();
        if (!dec || !dec->isCompleted()) {
            return false;
        }
    }
    if (videoDecoder_) {
        auto dec = videoDecoder_->decoder();
        if (!dec || !dec->isCompleted()) {
            return false;
        }
    }
    return true;
}

AVFrameRefPtr PlayPipeline::requestVideoFrame() noexcept {
    recordVsyncInterval();

    const auto state = pipelineState_.load(std::memory_order_acquire);
    if (state != PipelineState::Playing) {
        return nullptr;
    }

    if (!readiness_.firstFrameRendered.load(std::memory_order_acquire)) {
        return nullptr;
    }

    const double playbackRate = clockSync_->playbackRate();
    const double masterTime = clockSync_->masterTime().second();
    const double videoTime = clockSync_->videoTime().second();
    const double diff = masterTime - videoTime;
    const double cacheEnd = cacheEndTime();
    if (shouldHoldVideoFrame(diff, playbackRate)) {
        if (isDecodeCompleted()) {
            checkPlaybackComplete();
        }
        // 关键路径节流日志：避免逐帧刷屏，同时保留音画同步排查线索
        LogIThrottle(1000,
                     "requestVideoFrame HOLD, diff={:.3f}, masterTime={:.3f}, "
                     "videoTime={:.3f}, cacheEndTime={:.3f}, playbackRate={:.3f}",
                     diff, masterTime, videoTime, cacheEnd, playbackRate);
        return nullptr;
    }

    auto frame = popVideoFrame();
    if (!frame) {
        handleSeekFrameStarvation();
        LogI("requestVideoFrame SKIP: video queue empty (starvation), masterTime={:.3f} "
             "videoTime={:.3f} cacheEndTime={:.3f}",
             masterTime, videoTime, cacheEnd);
        bufferMonitor_->onStarvation(!config_.isLiveSource && config_.startBufferDuration == 0.0);
        return nullptr;
    }

    if (seekContext_->isSeekPending()) {
        frame = skipDiscardFramesDuringSeek(std::move(frame));
        if (!frame) {
            return nullptr;
        }
    }

    bufferMonitor_->resetStarvationCount();
    LogIThrottle(2000,
                 "requestVideoFrame POP pts={:.3f} dts={:.3f} isDiscard={}, masterTime={:.3f}, "
                 "videoTime={:.3f}, diff={:.3f}",
                 frame->ptsTime(), frame->dtsTime(), frame->isDiscard, masterTime, videoTime, diff);
    clockSync_->setVideoTime(Time::TimePoint::fromSeconds(frame->ptsTime()));

    markFirstFrameRenderedIfNeeded();

    if (seekContext_->isSeekPending() && !frame->isDiscard) {
        onSeekFrameConsumed(TrackType::Video);
        LogI("video seek completed");
    }

    if (mediaInfo_.hasAudio && !seekContext_->isSeekPending()) {
        const double diffAfter =
            clockSync_->masterTime().second() - clockSync_->videoTime().second();
        chaseVideoFrames(frame, diffAfter, playbackRate);
    }

    return frame;
}

void PlayPipeline::recordVsyncInterval() noexcept {
    auto vsyncNow = std::chrono::steady_clock::now();
    if (lastVsyncTime_ != std::chrono::steady_clock::time_point{}) {
        auto intervalUs =
            std::chrono::duration_cast<std::chrono::microseconds>(vsyncNow - lastVsyncTime_)
                .count();
        clockSync_->recordVsyncDuration(intervalUs);
    }
    lastVsyncTime_ = vsyncNow;
}

bool PlayPipeline::shouldHoldVideoFrame(double diff, double playbackRate) const noexcept {
    if (!readiness_.firstFrameRendered.load(std::memory_order_acquire)) {
        return false;
    }
    if (!mediaInfo_.hasAudio || seekContext_->isSeekPending()) {
        return false;
    }
    return ClockSync::evaluateAVSync(diff, playbackRate, ClockSync::kEarlyTolerance) ==
           AVSyncAction::Hold;
}

void PlayPipeline::chaseVideoFrames(AVFrameRefPtr& frame,
                                    double diff,
                                    double playbackRate) noexcept {
    const double videoFps = clockSync_->videoFps();
    const int64_t vsyncUs = clockSync_->metrics().vsyncDurationUs;
    constexpr double kFallbackHz = 60.0;
    const double displayHz = (vsyncUs > 0) ? (1e6 / static_cast<double>(vsyncUs)) : kFallbackHz;
    const int32_t framesPerVsync =
        (videoFps > 0.0 && displayHz > 0.0)
            ? std::max(1, static_cast<int32_t>(std::round(videoFps / displayHz)))
            : 1;
    const int32_t chaseLimit = framesPerVsync - 1;
    int32_t dropCount = 0;

    while (ClockSync::evaluateAVSync(diff, playbackRate) == AVSyncAction::Drop &&
           dropCount < config_.maxDropPerVsync) {
        auto nextFrame = popVideoFrame();
        if (!nextFrame) {
            break;
        }
        frame = std::move(nextFrame);
        clockSync_->setVideoTime(Time::TimePoint::fromSeconds(frame->ptsTime()));
        ++dropCount;

        if (chaseLimit > 0 && dropCount >= chaseLimit) {
            break;
        }
        diff = clockSync_->masterTime().second() - clockSync_->videoTime().second();
    }

    if (dropCount > 0) {
        clockSync_->recordDrop(dropCount);
        if (dropCount >= config_.maxDropPerVsync) {
            LogW("drop limit reached: dropped {} frames in one vsync", dropCount);
        }
    }
}

double PlayPipeline::demuxedDuration() const noexcept {
    const double currentSec = clockSync_->masterTime().second();
    return std::max(0.0, maxBufferedPts() - currentSec);
}

void PlayPipeline::pauseStream() noexcept {
    if (pauseStreamFunc_) {
        pauseStreamFunc_();
    }
}

void PlayPipeline::resumeStream() noexcept {
    if (resumeStreamFunc_) {
        resumeStreamFunc_();
    }
}

int32_t PlayPipeline::audioDecodedFrameCount() const noexcept {
    return audioFrames_.withLock([](const auto& q) {
        return static_cast<int32_t>(q.size());
    });
}

int32_t PlayPipeline::videoDecodedFrameCount() const noexcept {
    return videoFrames_.withLock([](const auto& m) {
        return static_cast<int32_t>(m.size());
    });
}

void PlayPipeline::pauseAudioDecoder() noexcept {
    if (audioDecoder_) {
        audioDecoder_->pause();
    }
}

void PlayPipeline::resumeAudioDecoder() noexcept {
    if (audioDecoder_) {
        audioDecoder_->resume();
    }
}

void PlayPipeline::pauseVideoDecoder() noexcept {
    if (videoDecoder_) {
        videoDecoder_->pause();
    }
}

void PlayPipeline::resumeVideoDecoder() noexcept {
    if (videoDecoder_) {
        videoDecoder_->resume();
    }
}

void PlayPipeline::firePipelineReady() noexcept {
    assert(isOnOwnerThread());
    if (readiness_.readyFired) {
        return;
    }
    readiness_.readyFired = true;
    SLARK_TRACE_MILESTONE(playSession_.lock().get(), "pipeline_ready");
    fireEvent(PipelineEvent::PipelineReady);
}

void PlayPipeline::onPrebufferComplete() noexcept {
    firePipelineReady();
}

void PlayPipeline::onDropOldest() noexcept {
    dropOldestFrames();
}

void PlayPipeline::onBufferEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept {
    if (!isOnOwnerThread()) {
        postToOwnerThread([this, event, error = std::move(error)]() mutable {
            fireEvent(event, std::move(error));
        });
        return;
    }
    fireEvent(event, std::move(error));
}

void PlayPipeline::onFastTick() noexcept {
    assert(isOnOwnerThread());
    if (pipelineState_.load(std::memory_order_acquire) >= PipelineState::TearingDown) {
        return;
    }
    if (auto* monitor = bufferMonitor_.get()) {
        monitor->onHeartbeat();
    }
    pushAudioFrames();
    pushSeekFrames();
}

void PlayPipeline::checkPlaybackComplete() noexcept {
    postToOwnerThread([this] {
        auto state = pipelineState_.load(std::memory_order_acquire);
        if (state >= PipelineState::Ended || state >= PipelineState::TearingDown) {
            return;
        }

        if (state != PipelineState::Playing && state != PipelineState::Paused) {
            return;
        }
        if (!readiness_.firstFrameRendered.load(std::memory_order_acquire)) {
            return;
        }

        if (!isAudioReadyForPlaybackEnd() || !isVideoReadyForPlaybackEnd()) {
            return;
        }
        if (!areQueuesDrained()) {
            return;
        }
        if (audioRender_ && mediaInfo_.hasAudio && audioRender_->readable() > 0) {
            return;
        }

        pipelineState_.store(PipelineState::Ended, std::memory_order_release);
        LogI("playback complete, firing RenderEnd");
        fireEvent(PipelineEvent::RenderEnd);
    });
}

bool PlayPipeline::isAudioReadyForPlaybackEnd() const noexcept {
    if (!audioDecoder_) {
        return true;
    }
    auto dec = audioDecoder_->decoder();
    if (dec && dec->isCompleted()) {
        return true;
    }
    // 视频已结束且音频数据排空时强制认定音频完成
    if (videoDecoder_) {
        if (auto videoDec = videoDecoder_->decoder(); videoDec && videoDec->isCompleted()) {
            bool audioQueueEmpty = true;
            audioFrames_.withLock([&audioQueueEmpty](auto& q) {
                audioQueueEmpty = q.empty();
            });
            if (audioQueueEmpty && audioRender_ && audioRender_->readable() == 0) {
                LogW("audio decoder not completed but video done and data drained, force complete");
                return true;
            }
        }
    }
    LogI("audio decoder not completed");
    return false;
}

bool PlayPipeline::isVideoReadyForPlaybackEnd() const noexcept {
    if (!videoDecoder_) {
        return true;
    }
    auto dec = videoDecoder_->decoder();
    if (dec && dec->isCompleted()) {
        return true;
    }
    LogI("video decoder not completed");
    return false;
}

bool PlayPipeline::areQueuesDrained() const noexcept {
    bool allEmpty = true;
    audioFrames_.withLock([&allEmpty](auto& q) {
        if (!q.empty()) {
            allEmpty = false;
        }
    });
    videoFrames_.withLock([&allEmpty](auto& m) {
        if (!m.empty()) {
            allEmpty = false;
        }
    });
    return allEmpty;
}

void PlayPipeline::handleSeekFrameStarvation() noexcept {
    if (!seekContext_->isSeekPending() || seekContext_->isVideoSeeked()) {
        return;
    }
    auto dec = videoDecoder_ ? videoDecoder_->decoder() : nullptr;
    if (dec && dec->isCompleted()) {
        onSeekFrameConsumed(TrackType::Video);
    }
}

AVFrameRefPtr PlayPipeline::skipDiscardFramesDuringSeek(AVFrameRefPtr frame) noexcept {
    while (frame->isDiscard) {
        auto next = popVideoFrame();
        if (!next) {
            // 所有帧均为 discard 且队列已空 → decoder 若已 EOF 则标记 seeked
            auto dec = videoDecoder_ ? videoDecoder_->decoder() : nullptr;
            if (dec && dec->isCompleted() && !seekContext_->isVideoSeeked()) {
                onSeekFrameConsumed(TrackType::Video);
            }
            return nullptr;
        }
        frame = std::move(next);
    }
    return frame;
}

void PlayPipeline::markFirstFrameRenderedIfNeeded() noexcept {
    bool expected = false;
    if (readiness_.firstFrameRendered.compare_exchange_strong(expected, true,
                                                              std::memory_order_acq_rel)) {
        if (auto session = playSession_.lock()) {
            session->markFirstFrame();
            SLARK_TRACE_MILESTONE(session.get(), "first_video_frame");
        }
        postToOwnerThread([this] {
            fireEvent(PipelineEvent::FirstFrameRendered);
        });
        LogI("first frame rendered");
    }
}

void PlayPipeline::notifySeekIfDecoderCompleted() noexcept {
    if (!seekContext_->isVideoSeeked()) {
        auto dec = videoDecoder_ ? videoDecoder_->decoder() : nullptr;
        if (dec && dec->isCompleted()) {
            onSeekFrameConsumed(TrackType::Video);
        }
    }
    if (!seekContext_->isAudioSeeked()) {
        auto dec = audioDecoder_ ? audioDecoder_->decoder() : nullptr;
        if (dec && dec->isCompleted()) {
            onSeekFrameConsumed(TrackType::Audio);
        }
    }
}

AVFrameRefPtr PlayPipeline::skipFramesBeforeTarget(AVFrameRefPtr frame, double target) noexcept {
    int32_t maxSkip = seekContext_->maxSkipFrames();
    int32_t skipped = 0;
    while ((frame && frame->ptsTime() < target) || skipped < maxSkip) {
        skipped++;
        frame = popVideoFrame();
    }
    return frame;
}

void PlayPipeline::drainPendingTasks() noexcept {
    assert(isOnOwnerThread());
    std::deque<std::function<void()>> tasks;
    pendingTasks_.withLock([&tasks](auto& q) {
        tasks.swap(q);
    });
    for (auto& task : tasks) {
        if (task) {
            task();
        }
    }
}

AVFrameRefPtr PlayPipeline::popVideoFrame() noexcept {
    AVFrameRefPtr frame;

    auto popFrame = [](auto& m) -> AVFrameRefPtr {
        if (m.empty()) {
            return nullptr;
        }
        auto frame = std::move(m.begin()->second);
        m.erase(m.begin());
        return frame;
    };

    videoFrames_.withLock([&frame, &popFrame](auto& m) {
        frame = popFrame(m);
    });

    if (!frame) {
        checkPlaybackComplete();
    }
    return frame;
}

bool PlayPipeline::isOnOwnerThread() const noexcept {
    return std::this_thread::get_id() == ownerThreadId_;
}

void PlayPipeline::setVideoRender(std::weak_ptr<IVideoRender> render) noexcept {
    videoRender_.store(render);
    if (auto r = videoRender_.load()) {
        r->setVideoTimeUpdater([this](Time::TimePoint t) {
            clockSync_->setVideoTime(t);
        });
    }
}

void PlayPipeline::setVolume(float volume) noexcept {
    if (audioRender_) {
        audioRender_->setVolume(volume);
    }
}

void PlayPipeline::setStreamControl(StreamControlFunc pauseStream,
                                    StreamControlFunc resumeStream) noexcept {
    pauseStreamFunc_ = std::move(pauseStream);
    resumeStreamFunc_ = std::move(resumeStream);
}

void PlayPipeline::setDemuxerComponent(std::shared_ptr<DemuxerComponent> demuxer) noexcept {
    demuxer_ = std::move(demuxer);
    std::tie(audioFrameSender_, audioFrameReceiver_) = Channel<AVFrameRefPtr>::create();
    std::tie(videoFrameSender_, videoFrameReceiver_) = Channel<AVFrameRefPtr>::create();
    connectComponents();
}

bool PlayPipeline::isDecodedFrameSkippable(const DecoderData& data) noexcept {
    if (pipelineState_.load(std::memory_order_acquire) >= PipelineState::TearingDown) {
        return true;
    }
    if (!data.frame) {
        if (data.error != Error::Success) {
            postToOwnerThread([this, err = data.error] {
                fireEvent(PipelineEvent::DecodeError, PipelineError{.error = err});
            });
        }
        return true;
    }
    if (data.frame->isEOS) {
        return true;
    }
    return seekContext_->isEpochStale(seekContext_->currentEpoch());
}

void PlayPipeline::onAudioDecoded(DecoderData data) noexcept {
    if (isDecodedFrameSkippable(data)) {
        return;
    }
    LogIThrottle(2000, "onAudioDecoded pts:{:.6f}, isSeekPending:{}", data.frame->ptsTime(),
                 seekContext_->isSeekPending());
    audioFrames_.withLock([&data](auto& q) {
        q.emplace_back(std::move(data.frame));
    });
    if (seekContext_->isSeekPending() && !seekContext_->isAudioSeeked()) {
        onSeekFrameConsumed(TrackType::Audio);
        LogI("audio seek completed");
    }
}

void PlayPipeline::setupAudioTrack(DecoderType type,
                                   std::shared_ptr<AudioDecoderConfig> config,
                                   std::shared_ptr<AudioInfo> audioInfo,
                                   float volume) noexcept {
    SAssert(!audioDecoder_, "audio track already set up");
    auto renderInfo = std::make_shared<AudioInfo>(*audioInfo);
    renderInfo->bitsPerSample = 16;
    audioRender_ = std::make_shared<AudioRenderComponent>(renderInfo);
    audioRender_->setVolume(volume);
    audioRender_->setAudioTimeUpdater([this](Time::TimePoint t) {
        clockSync_->setAudioTime(t, clockSync_->currentAudioEpoch());
    });
    audioRender_->setRenderEndCallback([this] {
        checkPlaybackComplete();
    });

    audioDecoder_ = std::make_shared<DecoderComponent>(
        std::move(audioFrameReceiver_),
        DecoderComponentConfig{
            .onData =
                [this](DecoderData data) {
                    onAudioDecoded(std::move(data));
                },
            .onReady =
                [this] {
                    postToOwnerThread([this] {
                        ++readiness_.readyDecoders;
                        SLARK_TRACE_MILESTONE(playSession_.lock().get(), "audio_decoder_ready");
                        checkAndFirePipelineReady();
                    });
                },
            .onError =
                [this](Error err) {
                    postToOwnerThread([this, err] {
                        fireEvent(PipelineEvent::AudioDecoderFailed, PipelineError{.error = err});
                    });
                },
            .isAudio = true,
        });

    openDecoderInternal(audioDecoder_, type, config);
    ++readiness_.expectedDecoders;
    registerComponents();
}

void PlayPipeline::handleSeekFirstFrame(const AVFrameRefPtr& frame,
                                        bool isSeekFirstFrame) noexcept {
    if (auto render = videoRender_.load()) {
        render->pushFrame(frame);
    }
    clockSync_->setVideoTime(Time::TimePoint::fromSeconds(frame->ptsTime()));
    bool expected = false;
    const bool needFirstFrame = readiness_.firstFrameRendered.compare_exchange_strong(
        expected, true, std::memory_order_acq_rel);
    postToOwnerThread([this, needFirstFrame, isSeekFirstFrame] {
        if (needFirstFrame) {
            if (auto session = playSession_.lock()) {
                session->markFirstFrame();
                SLARK_TRACE_MILESTONE(session.get(), "first_video_frame");
            }
            fireEvent(PipelineEvent::FirstFrameRendered);
            LogI("first frame rendered");
        }
        if (isSeekFirstFrame) {
            onSeekFrameConsumed(TrackType::Video);
            LogI("video seek completed");
        }
    });
}

void PlayPipeline::onVideoDecoded(DecoderData data) noexcept {
    if (isDecodedFrameSkippable(data)) {
        return;
    }
    LogIThrottle(2000, "onVideoDecoded pts:{:.6f}, isDiscard:{}, isSeekPending:{}",
                 data.frame->ptsTime(), data.frame->isDiscard, seekContext_->isSeekPending());

    if (!readiness_.firstFrameRendered.load(std::memory_order_acquire) &&
        !seekContext_->isSeekPending()) {
        handleSeekFirstFrame(data.frame, false);
        return;
    }

    videoFrames_.withLock([&data](auto& m) {
        m.emplace(data.frame->pts, std::move(data.frame));
    });
    if (seekContext_->isSeekPending() && !seekContext_->isVideoSeeked()) {
        postToOwnerThread([this]() {
            pushSeekFrames();
        });
    }
}

void PlayPipeline::setupVideoTrack(DecoderType type,
                                   std::shared_ptr<VideoDecoderConfig> config) noexcept {
    SAssert(!videoDecoder_, "video track already set up");

    if (config && config->numRefFrames > 0) {
        const int32_t refFrames = static_cast<int32_t>(config->numRefFrames);
        config_.videoDecoderLowWatermarkFrames = refFrames;
        config_.videoDecoderHighWatermarkFrames = std::max(refFrames * 2, refFrames + 4);
    }

    videoDecoder_ = std::make_shared<DecoderComponent>(
        std::move(videoFrameReceiver_),
        DecoderComponentConfig{
            .onData =
                [this](DecoderData data) {
                    onVideoDecoded(std::move(data));
                },
            .onReady =
                [this] {
                    postToOwnerThread([this] {
                        ++readiness_.readyDecoders;
                        SLARK_TRACE_MILESTONE(playSession_.lock().get(), "video_decoder_ready");
                        checkAndFirePipelineReady();
                    });
                },
            .onError =
                [this](Error err) {
                    postToOwnerThread([this, err] {
                        fireEvent(PipelineEvent::VideoDecoderFailed, PipelineError{.error = err});
                    });
                },
            .isAudio = false,
        });

    openDecoderInternal(videoDecoder_, type, config);
    ++readiness_.expectedDecoders;
    registerComponents();
}

void PlayPipeline::postToOwnerThread(std::function<void()> task) noexcept {
    if (pipelineState_.load(std::memory_order_acquire) >= PipelineState::TearingDown || !task) {
        return;
    }
    if (isOnOwnerThread()) {
        task();
        return;
    }
    pendingTasks_.withLock([&task](auto& q) {
        q.emplace_back(std::move(task));
    });
    if (wakeupCallback_) {
        wakeupCallback_();
    }
}

void PlayPipeline::fireEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept {
    assert(isOnOwnerThread() && "fireEvent must be called on ownerThread");
    static constexpr std::array kRequireError = {
        PipelineEvent::AudioDecoderFailed,
        PipelineEvent::VideoDecoderFailed,
        PipelineEvent::ReadError,
        PipelineEvent::DecodeError,
    };
    assert((std::find(kRequireError.begin(), kRequireError.end(), event) == kRequireError.end() ||
            error.has_value()) &&
           "error event must carry PipelineError");
    assert((std::find(kRequireError.begin(), kRequireError.end(), event) != kRequireError.end() ||
            !error.has_value()) &&
           "non-error event must not carry PipelineError");
    if (onEvent_) {
        onEvent_(event, std::move(error));
    }
}

void PlayPipeline::checkAndFirePipelineReady() noexcept {
    assert(isOnOwnerThread());
    if (readiness_.expectedDecoders == 0 ||
        readiness_.readyDecoders < readiness_.expectedDecoders) {
        return;
    }
    if (config_.isLiveSource && !checkLiveReadyCondition()) {
        return;
    }
    if (bufferMonitor_) {
        bufferMonitor_->startPrebuffering();
    } else {
        firePipelineReady();
    }
}

bool PlayPipeline::checkLiveReadyCondition() const noexcept {
    if (mediaInfo_.hasVideo && mediaInfo_.hasAudio) {
        return readiness_.liveVideoKeyframeSeen && readiness_.liveAudioPacketSeen;
    }
    if (mediaInfo_.hasVideo) {
        return readiness_.liveVideoKeyframeSeen;
    }
    if (mediaInfo_.hasAudio) {
        return readiness_.liveAudioPacketSeen;
    }
    return false;
}

void PlayPipeline::registerComponents() noexcept {
    components_.clear();
    if (demuxer_) {
        components_.push_back(demuxer_);
    }
    if (audioDecoder_) {
        components_.push_back(audioDecoder_);
    }
    if (videoDecoder_) {
        components_.push_back(videoDecoder_);
    }
    if (audioRender_) {
        components_.push_back(audioRender_);
    }
}

void PlayPipeline::connectComponents() noexcept {
    if (!demuxer_) {
        return;
    }
    demuxer_->setHandleResultFunc(
        [this](const std::shared_ptr<IDemuxer>& demuxer, DemuxerResult&& result) {
            if (!demuxer) {
                return;
            }
            handleDemuxHeader(demuxer);
            const uint64_t currentEpoch = seekContext_->currentEpoch();
            dispatchFrames(std::move(result), currentEpoch);
        });
}

void PlayPipeline::handleDemuxHeader(const std::shared_ptr<IDemuxer>& demuxer) noexcept {
    if (mediaInfo_.hasAudio || mediaInfo_.hasVideo) {
        return;
    }
    mediaInfo_.hasAudio = demuxer->hasAudio();
    mediaInfo_.hasVideo = demuxer->hasVideo();
    clockSync_->setHasAudio(mediaInfo_.hasAudio);
    auto duration = demuxer->totalDuration();
    if (duration.isValid()) {
        mediaInfo_.totalDuration = duration;
    }
    postToOwnerThread([this] {
        fireEvent(PipelineEvent::DemuxHeaderParsed);
        SLARK_TRACE_MILESTONE(playSession_.lock().get(), "demux_header_parsed");
    });
}

void PlayPipeline::updateLiveReadyFlags(const DemuxerResult& result) noexcept {
    if (!config_.isLiveSource) {
        return;
    }
    if (!result.audioFrames.empty()) {
        readiness_.liveAudioPacketSeen = true;
    }
    if (!readiness_.liveVideoKeyframeSeen) {
        for (const auto& frame : result.videoFrames) {
            if (frame && frame->info && frame->info->isKeyFrame()) {
                readiness_.liveVideoKeyframeSeen = true;
                break;
            }
        }
    }
}

bool PlayPipeline::dispatchAudioFrames(AVFramePtrArray& frames, uint64_t epoch) noexcept {
    if (!audioFrameSender_) {
        return false;
    }
    const auto seekTarget = seekContext_->targetSeekTime();
    bool enqueued = false;
    for (auto& frame : frames) {
        if (seekContext_->isEpochStale(epoch)) {
            break;
        }
        if (seekTarget && frame->ptsTime() < *seekTarget) {
            continue;
        }
        const double pts = frame->ptsTime();
        double prev = lastEnqueuedAudioPts_.load(std::memory_order_acquire);
        while (pts > prev &&
               !lastEnqueuedAudioPts_.compare_exchange_weak(prev, pts, std::memory_order_acq_rel)) {
        }
        audioFrameSender_->post(std::shared_ptr<AVFrame>(std::move(frame)));
        enqueued = true;
    }
    return enqueued;
}

bool PlayPipeline::dispatchVideoFrames(AVFramePtrArray& frames, uint64_t epoch) noexcept {
    if (!videoFrameSender_) {
        return false;
    }
    bool enqueued = false;
    const auto seekTarget = seekContext_->targetSeekTime();
    for (auto& frame : frames) {
        if (seekContext_->isEpochStale(epoch)) {
            break;
        }
        const double framePts = frame->ptsTime();
        if (frame->info && frame->info->isKeyFrame()) {
            seekContext_->updateGOPRange(framePts, seekContext_->gopEndPts());
        }
        if (framePts > seekContext_->gopEndPts()) {
            seekContext_->updateGOPRange(seekContext_->gopKeyframePts(), framePts);
        }

        if (seekTarget && framePts < *seekTarget) {
            frame->isDiscard = true;
        }
        double prev = lastEnqueuedVideoPts_.load(std::memory_order_acquire);
        while (framePts > prev && !lastEnqueuedVideoPts_.compare_exchange_weak(
                                      prev, framePts, std::memory_order_acq_rel)) {
        }
        videoFrameSender_->post(std::shared_ptr<AVFrame>(std::move(frame)));
        enqueued = true;
    }
    return enqueued;
}

void PlayPipeline::injectEosFrames() noexcept {
    if (eosInjected_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (audioFrameSender_) {
        auto eos = std::make_shared<AVFrame>(AVFrameType::Audio);
        eos->isEOS = true;
        auto audioInfo = std::make_shared<AudioFrameInfo>();
        audioInfo->isEndOfStream = true;
        eos->info = std::move(audioInfo);
        audioFrameSender_->post(std::move(eos));
    }
    if (videoFrameSender_) {
        auto eos = std::make_shared<AVFrame>(AVFrameType::Video);
        eos->isEOS = true;
        auto videoInfo = std::make_shared<VideoFrameInfo>();
        videoInfo->isEndOfStream = true;
        eos->info = std::move(videoInfo);
        videoFrameSender_->post(std::move(eos));
    }
}

void PlayPipeline::dispatchFrames(DemuxerResult&& result, uint64_t epoch) noexcept {
    updateLiveReadyFlags(result);

    bool hasAudio = dispatchAudioFrames(result.audioFrames, epoch);
    bool hasVideo = dispatchVideoFrames(result.videoFrames, epoch);

    if (result.resultCode == DemuxerResultCode::FileEnd) {
        injectEosFrames();
        hasAudio = hasAudio || audioDecoder_ != nullptr;
        hasVideo = hasVideo || videoDecoder_ != nullptr;
        if (bufferMonitor_) {
            bufferMonitor_->setDemuxEof();
        }
        LogI("demuxer file end");
    }

    if (hasAudio || hasVideo) {
        notifyDataEnqueued();
    }
}

void PlayPipeline::notifyDataEnqueued() noexcept {
    bool expected = false;
    if (dataEnqueuedPending_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        postToOwnerThread([this] {
            dataEnqueuedPending_.store(false, std::memory_order_release);
            if (pipelineState_.load(std::memory_order_acquire) >= PipelineState::TearingDown) {
                return;
            }
            if (bufferMonitor_) {
                bufferMonitor_->onDemuxedDataEnqueued();
            }
        });
    }
}

void PlayPipeline::openDecoderInternal(std::shared_ptr<DecoderComponent> decoder,
                                       DecoderType type,
                                       const std::shared_ptr<DecoderConfig>& config) noexcept {
    if (!decoder) {
        return;
    }
    decoder->open(type, config);
    if (decoder->isRunning()) {
        return;
    }
    decoder->start();
}

void PlayPipeline::stopComponents() noexcept {
    audioFrameSender_.reset();
    videoFrameSender_.reset();
    if (audioDecoder_) {
        audioDecoder_->stop();
    }
    if (videoDecoder_) {
        videoDecoder_->stop();
    }
    if (demuxer_) {
        // 注意：必须调 stop()（同步 join worker），否则 joinWithTimeout 会一直等到超时。
        // close() 仅清资源不停 worker，不能用在 teardown 路径上。
        demuxer_->stop();
    }
    if (audioRender_) {
        audioRender_->close();
    }
    if (auto render = videoRender_.load()) {
        render->close();
    }
}

bool PlayPipeline::joinWithTimeout(std::chrono::milliseconds timeout) noexcept {
    Time::Deadline deadline(timeout);
    while (!deadline.isExpired()) {
        bool allStopped = true;
        for (auto& comp : components_) {
            if (comp->isRunning()) {
                allStopped = false;
                break;
            }
        }
        if (allStopped) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

PlayPipeline::QueueBounds PlayPipeline::snapshotQueueBounds() noexcept {
    QueueBounds b{};
    videoFrames_.withLock([&](const auto& m) {
        if (!m.empty()) {
            b.videoOldestPts = m.begin()->second->ptsTime();
            b.maxPts = std::max(b.maxPts, m.rbegin()->second->ptsTime());
        }
    });
    audioFrames_.withLock([&](const auto& q) {
        if (!q.empty()) {
            b.audioOldestPts = q.front()->ptsTime();
            b.maxPts = std::max(b.maxPts, q.back()->ptsTime());
        }
    });
    return b;
}

int32_t PlayPipeline::dropFramesUpTo(double cutoffPts) noexcept {
    int32_t dropped = 0;
    videoFrames_.withLock([&](auto& m) {
        while (!m.empty() && m.begin()->second->ptsTime() <= cutoffPts) {
            m.erase(m.begin());
            ++dropped;
        }
    });
    return dropped;
}

void PlayPipeline::dropOldestFrames() noexcept {
    const double target = config_.demuxHighWatermarkDuration;
    const double currentSec = clockSync_->masterTime().second();
    int32_t droppedTotal = 0;

    while (true) {
        const auto bounds = snapshotQueueBounds();
        if (std::max(0.0, bounds.maxPts - currentSec) <= target) {
            break;
        }
        const double cutoffPts = std::max(bounds.audioOldestPts, bounds.videoOldestPts);
        if (cutoffPts < 0.0) {
            break;
        }
        const int32_t dropped = dropFramesUpTo(cutoffPts);
        droppedTotal += dropped;
        if (dropped == 0) {
            break;
        }
    }
    if (droppedTotal > 0) {
        LogW("hard-cap triggered: dropped {} frames to converge buffer", droppedTotal);
    }
}

void PlayPipeline::enterFaultedState() noexcept {
    pipelineState_.store(PipelineState::Faulted, std::memory_order_release);
    LogE("PlayPipeline entered Faulted state");
    if (isOnOwnerThread()) {
        fireEvent(PipelineEvent::ReadError, PipelineError{.error = Error::InvalidOperation});
    } else {
        pendingTasks_.withLock([self = this](auto& q) {
            q.emplace_back([self] {
                self->fireEvent(PipelineEvent::ReadError,
                                PipelineError{.error = Error::InvalidOperation});
            });
        });
    }
}

void PlayPipeline::pushAudioFrames() noexcept {
    if (!audioRender_ || pipelineState_ != PipelineState::Playing) {
        return;
    }
    bool hasPushed = false;
    while (true) {
        const auto avail = audioRender_->available();
        AVFrameRefPtr frame;
        audioFrames_.withLock([&](auto& q) {
            if (!q.empty() && q.front()->data && avail >= q.front()->data->length()) {
                frame = std::move(q.front());
                q.pop_front();
            }
        });
        if (!frame) {
            break;
        }
        hasPushed = true;
        const auto pushMasterTime = clockSync_->masterTime().second();
        if (!audioRender_->process(frame)) {
            LogW("pushAudioFrames: render process failed, pts={:.3f}, masterTime={:.3f}",
                 frame->ptsTime(), pushMasterTime);
            audioFrames_.withLock([&frame](auto& q) {
                q.emplace_front(std::move(frame));
            });
            break;
        }
        LogIThrottle(2000, "pushAudioFrames: pushed frame pts={:.3f}, masterTime={:.3f}",
                     frame->ptsTime(), pushMasterTime);
    }
    if (!hasPushed) {
        audioFrames_.withLock([&](auto& q) {
            if (q.empty() && audioDecoder_) {
                if (auto dec = audioDecoder_->decoder(); dec && dec->isCompleted()) {
                    audioRender_->setDecoderCompleted();
                }
            }
        });
        if (audioRender_->readable() == 0) {
            checkPlaybackComplete();
        }
    }
}

void PlayPipeline::pushSeekFrames() noexcept {
    if (!seekContext_->isSeekPending()) {
        return;
    }

    notifySeekIfDecoderCompleted();

    auto target = seekContext_->targetSeekTime();
    if (!target.has_value()) {
        return;
    }
    auto render = videoRender_.load();
    if (!render) {
        return;
    }

    auto frame = popVideoFrame();
    if (!frame) {
        return;
    }

    frame = skipFramesBeforeTarget(std::move(frame), *target);
    if (!frame) {
        return;
    }

    render->pushFrame(frame);
    clockSync_->setVideoTime(Time::TimePoint::fromSeconds(frame->ptsTime()));
    markFirstFrameRenderedIfNeeded();
    if (seekContext_->tryMarkVideoSeeked()) {
        onSeekFrameConsumed(TrackType::Video);
        LogI("video seek completed via pushSeekFrames");
    }
}

void PlayPipeline::onSeekFrameConsumed(TrackType track) noexcept {
    if (track == TrackType::Video) {
        seekContext_->markVideoSeeked();
    } else {
        seekContext_->markAudioSeeked();
    }
    if (!seekContext_->isAllTracksSeeked(mediaInfo_.hasVideo, mediaInfo_.hasAudio)) {
        return;
    }
    if (!seekContext_->completeSeek()) {
        return;
    }
    postToOwnerThread([this] {
        SLARK_TRACE_MILESTONE(playSession_.lock().get(), "seek_completed");
        fireEvent(PipelineEvent::SeekCompleted);
    });
}

}  // namespace slark
