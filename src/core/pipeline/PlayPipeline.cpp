//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayPipeline.h"

#include "AudioRenderComponent.h"
#include "BufferMonitor.h"
#include "DecoderComponent.h"
#include "DemuxerComponent.h"
#include "EpochFilter.h"
#include "Error.h"
#include "IDemuxer.h"
#include "Log.hpp"
#include "Thread.h"
#include "perf/Perf.h"

#include <cassert>
#include <chrono>

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
    // flush 后 ready 状态必须重置，允许重新触发 PipelineReady
    readyFired_.store(false, std::memory_order_release);
    decoderReadyCount_.store(0, std::memory_order_release);

    if (bufferMonitor_) {
        bufferMonitor_->reset();
    }
    for (auto& comp : components_) {
        comp->flush();
    }
}

void PlayPipeline::teardown() noexcept {
    if (pipelineState_ == PipelineState::Idle) {
        return;
    }
    teardownFlag_.store(true, std::memory_order_release);
    clockSync_->notifyTeardown();
    LogI("PlayPipeline teardown 开始");

    stopComponents();

    if (!joinWithTimeout(kTeardownTimeout)) {
        LogE("PlayPipeline teardown join 超时，进入 Faulted 终态");
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
    // 重建 BufferMonitor 以使新配置生效
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
    // 6步 seek 顺序：pause → flush → seekStream → seekDemuxer → resetClocks → resume
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
    auto vsyncStart = std::chrono::steady_clock::now();
    if (isolated_.load(std::memory_order_acquire)) {
        return nullptr;
    }

    // AV 同步：Hold 跳帧
    double diff = clockSync_->audioTime().second() - clockSync_->videoTime().second();
    if (mediaInfo_.hasAudio && !seekCoordinator_->isSeekPending()) {
        if (ClockSync::evaluateAVSync(diff, clockSync_->playbackRate()) == AVSyncAction::Hold) {
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

    // 追帧
    if (mediaInfo_.hasAudio && !seekCoordinator_->isSeekPending()) {
        diff = clockSync_->audioTime().second() - clockSync_->videoTime().second();
        int32_t dropCount = 0;
        while (ClockSync::evaluateAVSync(diff, clockSync_->playbackRate()) == AVSyncAction::Drop &&
               dropCount < config_.maxDropPerVsync) {
            auto nextFrame = popVideoFrame();
            if (!nextFrame) {
                break;
            }
            frame = std::move(nextFrame);
            clockSync_->setVideoTime(Time::TimePoint::fromSeconds(frame->ptsTime()));
            diff = clockSync_->audioTime().second() - clockSync_->videoTime().second();
            ++dropCount;
        }
        if (dropCount > 0) {
            clockSync_->recordDrop(dropCount);
        }
    }

    auto vsyncEnd = std::chrono::steady_clock::now();
    auto vsyncUs =
        std::chrono::duration_cast<std::chrono::microseconds>(vsyncEnd - vsyncStart).count();
    clockSync_->recordVsyncDuration(vsyncUs);

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

void PlayPipeline::setStreamControl(StreamControlFunc pauseStream,
                                    StreamControlFunc resumeStream) noexcept {
    pauseStreamFunc_ = std::move(pauseStream);
    resumeStreamFunc_ = std::move(resumeStream);
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
        LogW("seek timeout: seek did not complete within 5s");
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
    auto currentEpoch = seekCoordinator_->seekEpoch();

    // try_lock 退化策略：vsync 路径不阻塞
    bool locked = false;
    videoFrames_.withTryLock(
        [&frame, currentEpoch, &locked](auto& container) {
            frame = container.pop(currentEpoch);
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
                    [&frame, currentEpoch, &locked](auto& container) {
                        frame = container.pop(currentEpoch);
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

void PlayPipeline::setAudioRender(std::shared_ptr<AudioRenderComponent> render) noexcept {
    audioRender_ = std::move(render);
    if (audioRender_) {
        audioRender_->setRenderEndCallback([this] {
            // 音频系统线程回调，投递到 ownerThread
            if (!demuxer_ || !demuxer_->isCompleted()) {
                return;
            }
            bool expected = false;
            if (renderEndFired_.compare_exchange_strong(expected,
                                                        true,
                                                        std::memory_order_acq_rel)) {
                postToOwnerThread([this] {
                    fireEvent(PipelineEvent::RenderEnd);
                });
            }
        });
    }
}

void PlayPipeline::setDemuxerComponent(std::shared_ptr<DemuxerComponent> demuxer) noexcept {
    demuxer_ = std::move(demuxer);
}

void PlayPipeline::createAudioDecoder() noexcept {
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
    audioDecoder_->setErrorCallback([this](Error err, bool isVideo) {
        postToOwnerThread([this, err, isVideo] {
            fireEvent(
                PipelineEvent::DecodeError,
                PipelineError{
                    .event = PipelineEvent::DecodeError,
                    .stage = isVideo ? PipelineStage::VideoDecode : PipelineStage::AudioDecode,
                    .code = static_cast<int32_t>(err),
                    .message = "consecutive decode failure exceeded threshold",
                });
        });
    });
}

void PlayPipeline::createVideoDecoder() noexcept {
    videoDecoder_ = std::make_shared<DecoderComponent>([this](AVFrameRefPtr frame) {
        if (teardownFlag_.load(std::memory_order_acquire)) {
            return;
        }
        const uint64_t kFrameEpoch = seekCoordinator_->seekEpoch();
        if (EpochFilter::shouldDiscard(*seekCoordinator_, frame->ptsTime())) {
            return;
        }
        // 首次检测到 B 帧，切换容器策略
        if (!bFrameDetected_.load(std::memory_order_acquire)) {
            if (auto* vi = frame->videoInfo(); vi && vi->frameType == VideoFrameType::BFrame) {
                if (!bFrameDetected_.exchange(true, std::memory_order_acq_rel)) {
                    onBFrameDetected();
                }
            }
        }
        videoFrames_.withLock([&frame, kFrameEpoch](auto& container) {
            container.push(TaggedFrame{std::move(frame), kFrameEpoch});
        });
        // 通知渲染层有新帧可消费，替代 CADisplayLink 轮询
        if (auto render = videoRender_.load()) {
            render->onNewFrameAvailable();
        }
    });
    videoDecoder_->setErrorCallback([this](Error err, bool isVideo) {
        postToOwnerThread([this, err, isVideo] {
            fireEvent(
                PipelineEvent::DecodeError,
                PipelineError{
                    .event = PipelineEvent::DecodeError,
                    .stage = isVideo ? PipelineStage::VideoDecode : PipelineStage::AudioDecode,
                    .code = static_cast<int32_t>(err),
                    .message = "consecutive decode failure exceeded threshold",
                });
        });
    });
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
    // 必填载荷校验：这些事件必须携带 PipelineError
    assert((event != PipelineEvent::AudioDecoderFailed || error.has_value()) &&
           "AudioDecoderFailed must carry PipelineError");
    assert((event != PipelineEvent::VideoDecoderFailed || error.has_value()) &&
           "VideoDecoderFailed must carry PipelineError");
    assert((event != PipelineEvent::ReadError || error.has_value()) &&
           "ReadError must carry PipelineError");
    assert((event != PipelineEvent::DecodeError || error.has_value()) &&
           "DecodeError must carry PipelineError");
    // 反向校验：无载荷事件不得携带 PipelineError
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

    // decoder 全部就绪，通知 BufferMonitor 开始预缓冲
    // BufferMonitor 完成预缓冲后触发 PipelineReady（本地播放立即触发）
    if (bufferMonitor_) {
        bufferMonitor_->startPrebuffering();
    } else {
        // 无 BufferMonitor 时直接触发
        bool expectedFired = false;
        if (readyFired_.compare_exchange_strong(expectedFired,
                                                true,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
            SLARK_TRACE_MILESTONE(playSession_.lock().get(), "pipeline_ready");
            fireEvent(PipelineEvent::PipelineReady);
        }
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
    // videoRender_ 是 weak_ptr，生命周期由外部管理，不纳入 components_
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

            if (!mediaInfo_.hasAudio && !mediaInfo_.hasVideo) {
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

            // 快照当前 epoch，demux 包过滤与 decoder callback 保持同一代际隔离语义
            const uint64_t kCurrentEpoch = seekCoordinator_->seekEpoch();
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
                if (EpochFilter::shouldBreak(*seekCoordinator_, kCurrentEpoch)) {
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
                if (EpochFilter::shouldBreak(*seekCoordinator_, kCurrentEpoch)) {
                    break;
                }
                videoDecoder_->send(std::move(frame));
            }

            // 每次入队后触发背压检查
            if (!result.audioFrames.empty() || !result.videoFrames.empty()) {
                postToOwnerThread([this] {
                    if (bufferMonitor_) {
                        bufferMonitor_->onDataEnqueued();
                    }
                });
            }
        });
}

void PlayPipeline::openDecoder(std::shared_ptr<DecoderComponent> decoder,
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

void PlayPipeline::onBFrameDetected() noexcept {
    videoFrames_.withLock([](auto& container) {
        container.switchToSorted();
    });
    LogI("PlayPipeline: B 帧检测到，videoFrames_ 切换为 multimap 策略");
}

void PlayPipeline::initBufferMonitor() noexcept {
    bufferMonitor_ = std::make_unique<BufferMonitor>(
        config_,
        [this](PipelineEvent event, std::optional<PipelineError> error) {
            fireEvent(event, std::move(error));
        },
        [this]() -> double {
            return demuxedDuration();
        },
        pauseStreamFunc_ ? pauseStreamFunc_ : []() {},
        resumeStreamFunc_ ? resumeStreamFunc_ : []() {},
        [this]() {
            // 预缓冲完成，统一由 PlayPipeline 触发 PipelineReady（readyFired_ 防重）
            bool expectedFired = false;
            if (readyFired_.compare_exchange_strong(expectedFired,
                                                    true,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                SLARK_TRACE_MILESTONE(playSession_.lock().get(), "pipeline_ready");
                fireEvent(PipelineEvent::PipelineReady);
            }
        },
        [this]() {
            dropOldestFrames();
        });
}

double PlayPipeline::demuxedDuration() const noexcept {
    double maxPts = 0.0;
    bool hasAudioPts = false;
    audioFrames_.withLock([&maxPts, &hasAudioPts](const auto& q) {
        if (!q.empty()) {
            maxPts = std::max(maxPts, q.back()->ptsTime());
            hasAudioPts = true;
        }
    });
    // 纯视频流时 audioFrames_ 为空，回退到 videoFrames_ 尾帧 pts
    if (!hasAudioPts) {
        videoFrames_.withLock([&maxPts](const auto& container) {
            if (auto pts = container.backPts()) {
                maxPts = *pts;
            }
        });
    }
    const double kCurrentSec = clockSync_->masterTime(mediaInfo_.hasAudio).second();
    return std::max(0.0, maxPts - kCurrentSec);
}

void PlayPipeline::dropOldestFrames() noexcept {
    // hard-cap 收敛：丢最旧的音视频帧，直到缓冲量降至 highWatermark 以下
    const double kTarget = config_.highWatermarkDuration;
    int32_t droppedTotal = 0;
    while (demuxedDuration() > kTarget) {
        bool dropped = false;
        audioFrames_.withLock([&dropped](auto& q) {
            if (!q.empty()) {
                q.pop_front();
                dropped = true;
            }
        });
        videoFrames_.withLock([&dropped](auto& container) {
            if (!container.empty()) {
                container.popFront();
                dropped = true;
            }
        });
        if (!dropped) {
            break;
        }
        ++droppedTotal;
    }
    if (droppedTotal > 0) {
        LogW("hard-cap triggered: dropped {} frame pairs to converge buffer", droppedTotal);
    }
}

void PlayPipeline::enterFaultedState() noexcept {
    isolated_.store(true, std::memory_order_release);
    pipelineState_ = PipelineState::Faulted;
    LogE("PlayPipeline 进入 Faulted 终态");
    // Faulted 为终态事件，必须直达上层，不受 teardownFlag_ 拦截
    if (isOnOwnerThread()) {
        reportFaultedError();
    } else {
        // 绕过 teardownFlag_ 检查，直接入队 pendingTasks_
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
namespace slark
