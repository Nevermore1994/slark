//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayPipeline.h"

#include "AudioDecoderConfig.h"
#include "AudioRenderComponent.h"
#include "BufferMonitor.h"
#include "DecoderComponent.h"
#include "DemuxerComponent.h"
#include "EpochFilter.h"
#include "Error.h"
#include "IDemuxer.h"
#include "Log.hpp"
#include "Thread.h"
#include "VideoDecoderConfig.h"
#include "perf/Perf.h"

#include <cassert>
#include <chrono>
#include <map>

namespace slark {

namespace {
constexpr auto kTeardownTimeout = std::chrono::milliseconds(2000);
constexpr int32_t kErrInstanceFaulted = -1;
constexpr int32_t kTryLockFailThreshold = 3;
} // namespace

PlayPipeline::PlayPipeline(BackpressureConfig config) noexcept : config_(std::move(config)) {
    assert(config_.isValid() && "BackpressureConfig is invalid");
    clockSync_ = std::make_unique<ClockSync>();
    seekCoordinator_ = std::make_unique<SeekCoordinator>();
}

PlayPipeline::~PlayPipeline() {
    if (!teardownFlag_.load(std::memory_order_acquire)) {
        teardown();
    }
}

void PlayPipeline::build() noexcept {
    if (pipelineState_ != PipelineState::Idle) {
        return;
    }
    ownerThreadId_ = std::this_thread::get_id();
    initBufferMonitor();
    connectComponents();
    registerComponents();
    pipelineState_ = PipelineState::Built;
    LogI("PlayPipeline built");
}

void PlayPipeline::start() noexcept {
    assert(isOnOwnerThread());
    if (isolated_.load(std::memory_order_acquire)) {
        reportFaultedError();
        return;
    }
    if (pipelineState_ == PipelineState::Playing) {
        return;
    }
    for (auto& comp : components_) {
        comp->start();
    }
    if (auto render = videoRender_.load()) {
        render->start();
    }
    pipelineState_ = PipelineState::Playing;
}

void PlayPipeline::pause() noexcept {
    assert(isOnOwnerThread());
    if (isolated_.load(std::memory_order_acquire)) {
        reportFaultedError();
        return;
    }
    if (pipelineState_ == PipelineState::Paused || pipelineState_ == PipelineState::Built) {
        return;
    }
    for (auto& comp : components_) {
        comp->pause();
    }
    if (auto render = videoRender_.load()) {
        render->pause();
    }
    pipelineState_ = PipelineState::Paused;
}

void PlayPipeline::flush() noexcept {
    assert(isOnOwnerThread());
    if (isolated_.load(std::memory_order_acquire)) {
        reportFaultedError();
        return;
    }

    audioFrames_.withLock([](auto& q) {
        q.clear();
    });
    videoFrames_.withLock([](auto& m) {
        m.clear();
    });

    liveHasFirstVideoKeyframe_.store(false, std::memory_order_release);
    liveHasFirstAudioPacket_.store(false, std::memory_order_release);
    renderEndFired_.store(false, std::memory_order_release);

    if (bufferMonitor_) {
        bufferMonitor_->reset();
    }
    for (auto& comp : components_) {
        comp->flush();
    }
    if (auto render = videoRender_.load()) {
        render->flush();
    }
}

void PlayPipeline::teardown() noexcept {
    if (pipelineState_ == PipelineState::Idle) {
        return;
    }
    teardownFlag_.store(true, std::memory_order_release);
    LogI("PlayPipeline teardown");
    stopComponents();
    if (!joinWithTimeout(kTeardownTimeout)) {
        LogE("PlayPipeline teardown timeout, entering Faulted");
        enterFaultedState();
        return;
    }
    pipelineState_ = PipelineState::Idle;
}

void PlayPipeline::applyLiveConfig() noexcept {
    assert(isOnOwnerThread());
    if (config_.isLiveSource) {
        return;
    }
    config_ = BackpressureConfig::forLive();
    bufferMonitor_.reset();
    initBufferMonitor();
    LogI("PlayPipeline: switched to live backpressure config");
}

void PlayPipeline::seek(CTime target) noexcept {
    assert(isOnOwnerThread());
    if (isolated_.load(std::memory_order_acquire)) {
        reportFaultedError();
        return;
    }

    bool superseded = seekCoordinator_->beginSeek(target);
    if (superseded) {
        fireEvent(PipelineEvent::SeekTimeout);
    }
    SLARK_TRACE_MILESTONE(playSession_.lock().get(), "seek_start");
    // pause → flush → seekStream → seekDemuxer → resetClocks → resume
    pause();
    flush();
    for (auto& comp : components_) {
        comp->seekStream();
    }
    if (demuxer_) {
        auto pos = demuxer_->getSeekToPos(target.second());
        demuxer_->seekToPos(pos);
    }
    clockSync_->reset();
    start();

    LogI("seek to {:.3f}s, epoch={}", target.second(), seekCoordinator_->seekEpoch());
}

Time::TimePoint PlayPipeline::currentTime() noexcept {
    if (mediaInfo_.hasAudio && audioRender_) {
        auto time = audioRender_->playedTime();
        if (time.point() != 0) {
            return time;
        }
    }
    return clockSync_->videoTime();
}

AVFrameRefPtr PlayPipeline::requestVideoFrame() noexcept {
    auto vsyncNow = std::chrono::steady_clock::now();
    // 记录两次 VSync 回调的间隔，而非函数耗时
    if (lastVsyncTime_ != std::chrono::steady_clock::time_point{}) {
        auto intervalUs =
            std::chrono::duration_cast<std::chrono::microseconds>(vsyncNow - lastVsyncTime_)
                .count();
        clockSync_->recordVsyncDuration(intervalUs);
    }
    lastVsyncTime_ = vsyncNow;

    if (isolated_.load(std::memory_order_acquire)) {
        return nullptr;
    }

    const double playbackRate = clockSync_->playbackRate();
    double diff = audioMasterTime().second() - clockSync_->videoTime().second();
    if (mediaInfo_.hasAudio && !seekCoordinator_->isSeekPending()) {
        // 传入 kEarlyTolerance，消除 VSync 边界 ±2ms 抖动导致的误 Hold
        if (ClockSync::evaluateAVSync(diff, playbackRate, ClockSync::kEarlyTolerance) ==
            AVSyncAction::Hold) {
            return nullptr;
        }
    }

    auto frame = popVideoFrame();
    if (!frame) {
        bool isLocalSource = !config_.isLiveSource && config_.startBufferDuration == 0.0;
        bufferMonitor_->onStarvation(isLocalSource);
        return nullptr;
    }
    bufferMonitor_->resetStarvationCount();
    clockSync_->setVideoTime(Time::TimePoint::fromSeconds(frame->ptsTime()));

    if (auto session = playSession_.lock()) {
        session->markFirstFrame();
        SLARK_TRACE_MILESTONE(session.get(), "first_video_frame");
    }
    onSeekFrameConsumed(true);

    if (mediaInfo_.hasAudio && !seekCoordinator_->isSeekPending()) {
        diff = audioMasterTime().second() - clockSync_->videoTime().second();
        // 用实测 VSync 间隔推算屏幕刷新率，fallback 到 kVsyncHz
        double videoFps = clockSync_->videoFps();
        int64_t vsyncUs = clockSync_->metrics().vsyncDurationUs;
        constexpr double kFallbackHz = 60.0;
        double displayHz = (vsyncUs > 0) ? (1e6 / static_cast<double>(vsyncUs)) : kFallbackHz;
        // 每个 VSync 窗口内视频帧数：120fps/60Hz=2，240fps/60Hz=4，180fps/60Hz=3
        // 用于限制单次追帧上限，防止一个 VSync 内丢过多帧导致画面跳跃
        int32_t framesPerVsync =
            (videoFps > 0.0 && displayHz > 0.0)
                ? std::max(1, static_cast<int32_t>(std::round(videoFps / displayHz)))
                : 1;
        // 每个 VSync 最多追 framesPerVsync-1 帧，至少保留 1 帧渲染
        int32_t chaseLimit = framesPerVsync - 1;
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

            // 达到本 VSync 窗口追帧上限，强制保留当前帧退出，避免画面跳跃
            if (chaseLimit > 0 && dropCount >= chaseLimit) {
                break;
            }

            diff = audioMasterTime().second() - clockSync_->videoTime().second();
        }
        if (dropCount > 0) {
            clockSync_->recordDrop(dropCount);
            if (dropCount >= config_.maxDropPerVsync) {
                LogW("drop limit reached: dropped {} frames in one vsync", dropCount);
            }
        }
    }

    return frame;
}

AVFrameRefPtr PlayPipeline::requestAudioFrame() noexcept {
    if (isolated_.load(std::memory_order_acquire)) {
        return nullptr;
    }
    AVFrameRefPtr frame;
    audioFrames_.withLock([&frame](auto& q) {
        if (!q.empty()) {
            frame = std::move(q.front());
            q.pop_front();
        }
    });
    if (frame) {
        clockSync_->setAudioTime(Time::TimePoint::fromSeconds(frame->ptsTime()));
        onSeekFrameConsumed(false);
    }
    return frame;
}

double PlayPipeline::demuxedDuration() const noexcept {
    double maxPts = 0.0;
    audioFrames_.withLock([&maxPts](const auto& q) {
        if (!q.empty()) {
            maxPts = q.back()->ptsTime();
        }
    });
    videoFrames_.withLock([&maxPts](const auto& m) {
        if (!m.empty()) {
            maxPts = std::max(maxPts, m.rbegin()->second->ptsTime());
        }
    });
    // 纳入待解码队列中最新帧的 pts，确保背压水位覆盖 demux → decode 这一层缓冲
    if (audioDecoder_) {
        auto pts = audioDecoder_->pendingMaxPts();
        if (pts >= 0.0) {
            maxPts = std::max(maxPts, pts);
        }
    }
    if (videoDecoder_) {
        auto pts = videoDecoder_->pendingMaxPts();
        if (pts >= 0.0) {
            maxPts = std::max(maxPts, pts);
        }
    }
    const double kCurrentSec = clockSync_->masterTime(mediaInfo_.hasAudio).second();
    return std::max(0.0, maxPts - kCurrentSec);
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

void PlayPipeline::firePipelineReady() noexcept {
    bool expected = false;
    if (readyFired_.compare_exchange_strong(expected,
                                            true,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
        SLARK_TRACE_MILESTONE(playSession_.lock().get(), "pipeline_ready");
        fireEvent(PipelineEvent::PipelineReady);
    }
}

void PlayPipeline::onPrebufferComplete() noexcept {
    firePipelineReady();
}

void PlayPipeline::onDropOldest() noexcept {
    dropOldestFrames();
}

void PlayPipeline::onBufferEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept {
    fireEvent(event, std::move(error));
}

void PlayPipeline::onHeartbeat() noexcept {
    assert(isOnOwnerThread());
    if (bufferMonitor_) {
        bufferMonitor_->onHeartbeat();
    }
    // seek 超时检测：5s 未完成则触发 SeekTimeout
    constexpr auto kSeekTimeout = std::chrono::milliseconds(5000);
    if (seekCoordinator_->isSeekTimedOut(kSeekTimeout)) {
        seekCoordinator_->completeSeek();
        fireEvent(PipelineEvent::SeekTimeout);
        LogW("seek timeout");
    }
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

    // vsync 路径不阻塞，try_lock 失败时有界自旋
    bool locked = false;
    videoFrames_.withTryLock(
        [&frame, &popFrame, &locked](auto& m) {
            frame = popFrame(m);
            locked = true;
        },
        [] {
        });

    if (locked) {
        consecutiveTryLockFailures_.store(0, std::memory_order_release);
    } else {
        auto failures = consecutiveTryLockFailures_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (failures >= kTryLockFailThreshold) {
            constexpr auto kSpinBudget60Hz = std::chrono::microseconds(500);
            constexpr auto kSpinBudget120Hz = std::chrono::microseconds(250);
            auto spinBudget = mediaInfo_.fps >= 120 ? kSpinBudget120Hz : kSpinBudget60Hz;
            auto deadline = std::chrono::steady_clock::now() + spinBudget;
            while (std::chrono::steady_clock::now() <= deadline) {
                videoFrames_.withTryLock(
                    [&frame, &popFrame, &locked](auto& m) {
                        frame = popFrame(m);
                        locked = true;
                    },
                    [] {
                    });
                if (locked) {
                    consecutiveTryLockFailures_.store(0, std::memory_order_release);
                    break;
                }
                std::this_thread::yield();
            }
        }
    }

    if (!frame && !mediaInfo_.hasAudio && demuxer_ && demuxer_->isCompleted()) {
        bool expected = false;
        if (renderEndFired_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            postToOwnerThread([this] {
                fireEvent(PipelineEvent::RenderEnd);
            });
        }
    }
    return frame;
}

bool PlayPipeline::isOnOwnerThread() const noexcept {
    return std::this_thread::get_id() == ownerThreadId_;
}

void PlayPipeline::setVideoRender(std::weak_ptr<IVideoRender> render) noexcept {
    videoRender_.store(render);
}

void PlayPipeline::setVolume(float volume) noexcept {
    if (audioRender_) {
        audioRender_->setVolume(volume);
    }
}

Time::TimePoint PlayPipeline::audioPlayedTime() noexcept {
    if (audioRender_) {
        return audioRender_->playedTime();
    }
    return {};
}

void PlayPipeline::setDemuxerComponent(std::shared_ptr<DemuxerComponent> demuxer) noexcept {
    demuxer_ = std::move(demuxer);
    connectComponents();
    registerComponents();
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
    audioRender_->setPullDataFunc([this](uint32_t) {
        return requestAudioFrame();
    });
    audioRender_->setRenderEndCallback([this] {
        if (!demuxer_ || !demuxer_->isCompleted()) {
            return;
        }
        bool expected = false;
        if (renderEndFired_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            postToOwnerThread([this] {
                fireEvent(PipelineEvent::RenderEnd);
            });
        }
    });

    audioDecoder_ = std::make_shared<DecoderComponent>([this](AVFrameRefPtr frame) {
        if (teardownFlag_.load(std::memory_order_acquire)) {
            return;
        }
        const uint64_t kFrameEpoch = seekCoordinator_->seekEpoch();
        if (EpochFilter::shouldDiscard(*seekCoordinator_, frame->ptsTime())) {
            return;
        }
        audioFrames_.withLock([&frame, kFrameEpoch, this](auto& q) {
            if (EpochFilter::shouldBreak(*seekCoordinator_, kFrameEpoch)) {
                return;
            }
            q.emplace_back(std::move(frame));
        });
    });
    audioDecoder_->setErrorCallback([this](Error err, bool /*fatal*/) {
        postToOwnerThread([this, err] {
            fireEvent(PipelineEvent::DecodeError,
                      PipelineError{
                          .event = PipelineEvent::DecodeError,
                          .stage = PipelineStage::AudioDecode,
                          .code = static_cast<int32_t>(err),
                          .message = "consecutive decode failure exceeded threshold",
                      });
        });
    });

    registerComponents();
    openDecoderInternal(audioDecoder_, type, config, false);
}

void PlayPipeline::setupVideoTrack(DecoderType type,
                                   std::shared_ptr<VideoDecoderConfig> config) noexcept {
    SAssert(!videoDecoder_, "video track already set up");
    videoDecoder_ = std::make_shared<DecoderComponent>([this](AVFrameRefPtr frame) {
        if (teardownFlag_.load(std::memory_order_acquire)) {
            return;
        }
        if (EpochFilter::shouldDiscard(*seekCoordinator_, frame->ptsTime())) {
            return;
        }
        videoFrames_.withLock([&frame](auto& m) {
            m.emplace(frame->pts, std::move(frame));
        });
        if (auto render = videoRender_.load()) {
            render->onNewFrameAvailable();
        }
    });
    videoDecoder_->setErrorCallback([this](Error err, bool /*fatal*/) {
        postToOwnerThread([this, err] {
            fireEvent(PipelineEvent::DecodeError,
                      PipelineError{
                          .event = PipelineEvent::DecodeError,
                          .stage = PipelineStage::VideoDecode,
                          .code = static_cast<int32_t>(err),
                          .message = "consecutive decode failure exceeded threshold",
                      });
        });
    });

    registerComponents();
    openDecoderInternal(videoDecoder_, type, config, true);
}

void PlayPipeline::postToOwnerThread(std::function<void()> task) noexcept {
    if (teardownFlag_.load(std::memory_order_acquire) || !task) {
        return;
    }
    if (isOnOwnerThread()) {
        task();
        return;
    }
    pendingTasks_.withLock([&task](auto& q) {
        q.emplace_back(std::move(task));
    });
}

void PlayPipeline::fireEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept {
    assert(isOnOwnerThread() && "fireEvent must be called on ownerThread");
    assert((event != PipelineEvent::AudioDecoderFailed || error.has_value()) &&
           "AudioDecoderFailed must carry PipelineError");
    assert((event != PipelineEvent::VideoDecoderFailed || error.has_value()) &&
           "VideoDecoderFailed must carry PipelineError");
    assert((event != PipelineEvent::ReadError || error.has_value()) &&
           "ReadError must carry PipelineError");
    assert((event != PipelineEvent::DecodeError || error.has_value()) &&
           "DecodeError must carry PipelineError");
    assert((event == PipelineEvent::AudioDecoderFailed ||
            event == PipelineEvent::VideoDecoderFailed || event == PipelineEvent::ReadError ||
            event == PipelineEvent::DecodeError || !error.has_value()) &&
           "non-error event must not carry PipelineError");
    if (onEvent_) {
        onEvent_(event, std::move(error));
    }
}

void PlayPipeline::checkAndFirePipelineReady() noexcept {
    assert(isOnOwnerThread());

    int32_t expected = 0;
    if (mediaInfo_.hasAudio) {
        ++expected;
    }
    if (mediaInfo_.hasVideo) {
        ++expected;
    }
    if (expected == 0) {
        return;
    }

    if (config_.isLiveSource && !checkLiveReadyCondition()) {
        return;
    }
    if (decoderReadyCount_.load(std::memory_order_acquire) < expected) {
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
        return liveHasFirstVideoKeyframe_.load(std::memory_order_acquire) &&
               liveHasFirstAudioPacket_.load(std::memory_order_acquire);
    }
    if (mediaInfo_.hasVideo) {
        return liveHasFirstVideoKeyframe_.load(std::memory_order_acquire);
    }
    if (mediaInfo_.hasAudio) {
        return liveHasFirstAudioPacket_.load(std::memory_order_acquire);
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
            const uint64_t kCurrentEpoch = seekCoordinator_->seekEpoch();
            dispatchFrames(std::move(result), kCurrentEpoch);
        });
}

void PlayPipeline::handleDemuxHeader(const std::shared_ptr<IDemuxer>& demuxer) noexcept {
    if (mediaInfo_.hasAudio || mediaInfo_.hasVideo) {
        return;
    }
    mediaInfo_.hasAudio = demuxer->hasAudio();
    mediaInfo_.hasVideo = demuxer->hasVideo();
    auto duration = demuxer->totalDuration();
    if (duration.isValid()) {
        mediaInfo_.totalDuration = duration;
    }
    postToOwnerThread([this] {
        fireEvent(PipelineEvent::DemuxHeaderParsed);
        SLARK_TRACE_MILESTONE(playSession_.lock().get(), "demux_header_parsed");
    });
}

void PlayPipeline::dispatchFrames(DemuxerResult&& result, uint64_t epoch) noexcept {
    for (auto& frame : result.audioFrames) {
        if (!audioDecoder_) {
            continue;
        }
        if (config_.isLiveSource) {
            liveHasFirstAudioPacket_.store(true, std::memory_order_release);
        }
        if (EpochFilter::shouldDiscard(*seekCoordinator_, frame->ptsTime())) {
            continue;
        }
        if (EpochFilter::shouldBreak(*seekCoordinator_, epoch)) {
            break;
        }
        audioDecoder_->send(std::move(frame));
    }
    for (auto& frame : result.videoFrames) {
        if (!videoDecoder_) {
            continue;
        }
        if (config_.isLiveSource && frame && frame->isKeyFrame()) {
            liveHasFirstVideoKeyframe_.store(true, std::memory_order_release);
        }
        if (EpochFilter::shouldDiscard(*seekCoordinator_, frame->ptsTime())) {
            continue;
        }
        if (EpochFilter::shouldBreak(*seekCoordinator_, epoch)) {
            break;
        }
        videoDecoder_->send(std::move(frame));
    }
    if (!result.audioFrames.empty() || !result.videoFrames.empty()) {
        notifyDataEnqueued();
    }
}

void PlayPipeline::notifyDataEnqueued() noexcept {
    bool expected = false;
    if (dataEnqueuedPending_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        postToOwnerThread([this] {
            dataEnqueuedPending_.store(false, std::memory_order_release);
            if (bufferMonitor_) {
                bufferMonitor_->onDataEnqueued();
            }
        });
    }
}

void PlayPipeline::openDecoderInternal(std::shared_ptr<DecoderComponent> decoder,
                                       DecoderType type,
                                       const std::shared_ptr<DecoderConfig>& config,
                                       bool isVideo) noexcept {
    if (!decoder) {
        return;
    }
    decoder->open(type, config, [this, isVideo](DecoderOpenResult result) mutable {
        postToOwnerThread([this, isVideo, result = std::move(result)] {
            if (!result.success) {
                auto event =
                    isVideo ? PipelineEvent::VideoDecoderFailed : PipelineEvent::AudioDecoderFailed;
                auto stage = isVideo ? PipelineStage::VideoDecode : PipelineStage::AudioDecode;
                fireEvent(event,
                          PipelineError{
                              .event = event,
                              .stage = stage,
                              .code = static_cast<int32_t>(result.error),
                              .message = result.message,
                          });
                return;
            }
            decoderReadyCount_.fetch_add(1, std::memory_order_acq_rel);
            SLARK_TRACE_MILESTONE(playSession_.lock().get(),
                                  isVideo ? "video_decoder_ready" : "audio_decoder_ready");
            checkAndFirePipelineReady();
        });
    });
}

void PlayPipeline::stopComponents() noexcept {
    for (auto& comp : components_) {
        comp->close();
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

void PlayPipeline::initBufferMonitor() noexcept {
    bufferMonitor_ = std::make_unique<BufferMonitor>(config_, *this);
}

void PlayPipeline::dropOldestFrames() noexcept {
    const double kTarget = config_.highWatermarkDuration;
    const double kCurrentSec = clockSync_->masterTime(mediaInfo_.hasAudio).second();
    int32_t droppedTotal = 0;

    while (true) {
        // 每轮先读取两端队列中最老帧的 pts，取较大值作为截断点
        // 这样保证音视频在 pts 上对齐丢弃，避免一路多丢一路少丢
        double audioOldestPts = -1.0;
        double videoOldestPts = -1.0;
        audioFrames_.withLock([&audioOldestPts](const auto& q) {
            if (!q.empty()) {
                audioOldestPts = q.front()->ptsTime();
            }
        });
        videoFrames_.withLock([&videoOldestPts](const auto& m) {
            if (!m.empty()) {
                videoOldestPts = m.begin()->second->ptsTime();
            }
        });

        // 计算当前缓冲时长：取两端最新帧 pts 中的较大值
        double maxPts = 0.0;
        audioFrames_.withLock([&maxPts](const auto& q) {
            if (!q.empty()) {
                maxPts = q.back()->ptsTime();
            }
        });
        videoFrames_.withLock([&maxPts](const auto& m) {
            if (!m.empty()) {
                maxPts = std::max(maxPts, m.rbegin()->second->ptsTime());
            }
        });
        const double kDuration = std::max(0.0, maxPts - kCurrentSec);
        if (kDuration <= kTarget) {
            break;
        }

        // 按 pts 对齐：丢弃 pts 不超过截断点的帧
        // 截断点取两端最老帧的较大值，保证一帧对齐
        double cutoffPts = std::max(audioOldestPts, videoOldestPts);
        if (cutoffPts < 0.0) {
            break;
        }

        int32_t audioDropped = 0;
        int32_t videoDropped = 0;
        audioFrames_.withLock([&cutoffPts, &audioDropped](auto& q) {
            while (!q.empty() && q.front()->ptsTime() <= cutoffPts) {
                q.pop_front();
                ++audioDropped;
            }
        });
        videoFrames_.withLock([&cutoffPts, &videoDropped](auto& m) {
            while (!m.empty() && m.begin()->second->ptsTime() <= cutoffPts) {
                m.erase(m.begin());
                ++videoDropped;
            }
        });

        droppedTotal += audioDropped + videoDropped;
        if (audioDropped == 0 && videoDropped == 0) {
            break;
        }
    }
    if (droppedTotal > 0) {
        LogW("hard-cap triggered: dropped {} frames to converge buffer", droppedTotal);
    }
}

void PlayPipeline::enterFaultedState() noexcept {
    isolated_.store(true, std::memory_order_release);
    pipelineState_ = PipelineState::Faulted;
    LogE("PlayPipeline entered Faulted state");
    // Faulted 为终态，绕过 teardownFlag_ 直达上层
    if (isOnOwnerThread()) {
        reportFaultedError();
    } else {
        pendingTasks_.withLock([this](auto& q) {
            q.emplace_back([this] {
                fireEvent(PipelineEvent::ReadError,
                          PipelineError{
                              .event = PipelineEvent::ReadError,
                              .stage = PipelineStage::Demux,
                              .code = kErrInstanceFaulted,
                              .message = "instance faulted",
                          });
            });
        });
    }
}

Time::TimePoint PlayPipeline::audioMasterTime() const noexcept {
    if (mediaInfo_.hasAudio && audioRender_) {
        auto t = audioRender_->playedTime();
        if (t.point() != 0) {
            return t;
        }
    }
    return clockSync_->audioTime();
}

void PlayPipeline::reportFaultedError() noexcept {
    assert(isOnOwnerThread());
    fireEvent(PipelineEvent::ReadError,
              PipelineError{
                  .event = PipelineEvent::ReadError,
                  .stage = PipelineStage::Demux,
                  .code = kErrInstanceFaulted,
                  .message = "instance faulted",
              });
}

void PlayPipeline::onSeekFrameConsumed(bool isVideo) noexcept {
    if (!seekCoordinator_->isSeekPending()) {
        return;
    }
    if (isVideo) {
        seekCoordinator_->onVideoSeeked();
    } else {
        seekCoordinator_->onAudioSeeked();
    }
    if (seekCoordinator_->isSeekCompleted(mediaInfo_.hasVideo, mediaInfo_.hasAudio)) {
        seekCoordinator_->completeSeek();
        postToOwnerThread([this] {
            SLARK_TRACE_MILESTONE(playSession_.lock().get(), "seek_completed");
            fireEvent(PipelineEvent::SeekCompleted);
        });
    }
}

} // namespace slark
