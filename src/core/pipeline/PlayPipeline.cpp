//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayPipeline.h"

#include "AudioRenderComponent.h"
#include "BufferMonitor.h"
#include "DecoderComponent.h"
#include "DemuxerComponent.h"
#include "Error.h"
#include "IDemuxer.h"
#include "Log.hpp"
#include "Thread.h"
#include "perf/Perf.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <map>

namespace slark {

namespace {
constexpr auto kTeardownTimeout = std::chrono::milliseconds(2000);
} // namespace

PlayPipeline::PlayPipeline(BackpressureConfig config) noexcept : config_(std::move(config)) {
    assert(config_.isValid() && "BackpressureConfig is invalid");
    clockSync_ = std::make_unique<ClockSync>();
    seekContext_ = std::make_unique<SeekContext>();
}

PlayPipeline::~PlayPipeline() {
    const auto s = pipelineState_.load(std::memory_order_acquire);
    if (s != PipelineState::Idle && s != PipelineState::TearingDown) {
        teardown();
    }
}

void PlayPipeline::build() noexcept {
    if (pipelineState_.load(std::memory_order_acquire) != PipelineState::Idle) {
        return;
    }
    ownerThreadId_ = std::this_thread::get_id();
    bufferMonitor_ = std::make_unique<BufferMonitor>(config_, *this);
    connectComponents();
    pipelineState_.store(PipelineState::Built, std::memory_order_release);
    LogI("PlayPipeline built");
}

void PlayPipeline::start() noexcept {
    assert(isOnOwnerThread());
    if (pipelineState_.load(std::memory_order_acquire) == PipelineState::Faulted) {
        fireEvent(PipelineEvent::ReadError, PipelineError{.error = Error::InvalidOperation});
        return;
    }
    if (pipelineState_.load(std::memory_order_acquire) == PipelineState::Playing) {
        return;
    }
    if (demuxer_) {
        demuxer_->start();
    }
    if (audioDecoder_) {
        audioDecoder_->start();
    }
    if (videoDecoder_) {
        videoDecoder_->start();
    }
    if (audioRender_) {
        audioRender_->start();
    }
    if (auto render = videoRender_.load()) {
        render->start();
    }
    pipelineState_.store(PipelineState::Playing, std::memory_order_release);
}

void PlayPipeline::pause() noexcept {
    assert(isOnOwnerThread());
    const auto s = pipelineState_.load(std::memory_order_acquire);
    if (s == PipelineState::Faulted) {
        fireEvent(PipelineEvent::ReadError, PipelineError{.error = Error::InvalidOperation});
        return;
    }
    if (s == PipelineState::Paused || s == PipelineState::Built) {
        return;
    }
    if (demuxer_) {
        demuxer_->pause();
    }
    if (audioDecoder_) {
        audioDecoder_->pause();
    }
    if (videoDecoder_) {
        videoDecoder_->pause();
    }
    if (audioRender_) {
        audioRender_->pause();
    }
    if (auto render = videoRender_.load()) {
        render->pause();
    }
    pipelineState_.store(PipelineState::Paused, std::memory_order_release);
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
    pendingDecodeAudioQueue_.withLock([](auto& q) {
        q.clear();
    });
    pendingDecodeVideoQueue_.withLock([](auto& q) {
        q.clear();
    });

    // seek/flush 后重置直播首帧标志；readyFired 保持不变（不重复触发 Ready）
    // firstFrameRendered 也重置，seek 后需要重新走首帧快速推帧路径
    readiness_.liveVideoKeyframeSeen = false;
    readiness_.liveAudioPacketSeen = false;
    readiness_.firstFrameRendered.store(false, std::memory_order_release);
    // flush 后 GOP 范围失效，重置
    seekContext_->updateGOPRange(-1.0, -1.0);

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
        audioRender_->flush();
    }
    if (auto render = videoRender_.load()) {
        render->flush();
    }
}

void PlayPipeline::teardown() noexcept {
    if (pipelineState_.load(std::memory_order_acquire) == PipelineState::Idle) {
        return;
    }
    pipelineState_.store(PipelineState::TearingDown, std::memory_order_release);
    LogI("PlayPipeline teardown");
    stopComponents();
    if (!joinWithTimeout(kTeardownTimeout)) {
        LogE("PlayPipeline teardown timeout, entering Faulted");
        enterFaultedState();
        return;
    }
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
    const double currentSec = clockSync_->masterTime(mediaInfo_.hasAudio).second();
    const double gopKeyframe = seekContext_->gopKeyframePts();
    const double gopEnd = seekContext_->gopEndPts();
    const bool inGOP = gopEnd > 0.0 && targetSec >= gopKeyframe && targetSec <= gopEnd;
    if (inGOP && targetSec > currentSec) {
        // GOP 内向后 seek：直接快速推帧，无需 demuxer seek
        return SeekStrategy::FastForwardInGOP;
    }
    if (inGOP && targetSec <= currentSec) {
        // GOP 内向前 seek：需要 seek 回本 GOP 关键帧，再快速推帧到目标
        return SeekStrategy::SeekToGOPKeyframe;
    }
    return SeekStrategy::SeekToKeyframe;
}

void PlayPipeline::seek(CTime target, bool isAccurate) noexcept {
    assert(isOnOwnerThread());
    if (pipelineState_.load(std::memory_order_acquire) == PipelineState::Faulted) {
        fireEvent(PipelineEvent::ReadError, PipelineError{.error = Error::InvalidOperation});
        return;
    }

    const double targetSec = target.second();
    const double currentSec = clockSync_->masterTime(mediaInfo_.hasAudio).second();

    constexpr double kMinSeekDistance = 0.05;
    if (std::abs(targetSec - currentSec) <= kMinSeekDistance) {
        return;
    }

    if (seekContext_->isSeekPending() && !isAccurate) {
        LogI("seek pending, ignored: target={:.3f}s, isAccurate={}", targetSec, isAccurate);
        return;
    }

    const SeekStrategy strategy = resolveSeekStrategy(targetSec);
    [[maybe_unused]] const bool seekStarted = seekContext_->beginSeek(target, strategy);
    SLARK_TRACE_MILESTONE(playSession_.lock().get(), "seek_start");

    if (bufferMonitor_) {
        bufferMonitor_->pauseBackpressure();
    }

    if (strategy == SeekStrategy::FastForwardInGOP) {
        clockSync_->reset();
        LogI("seek fast-forward in GOP to {:.3f}s, epoch={}",
             targetSec,
             seekContext_->currentEpoch());
    } else {
        // SeekToKeyframe / SeekToGOPKeyframe：pause → flush → reset → seekDemuxer → resume
        pause();
        flush();
        clockSync_->reset();
        if (demuxer_) {
            const double seekPos = (strategy == SeekStrategy::SeekToGOPKeyframe)
                                       ? seekContext_->gopKeyframePts()
                                       : targetSec;
            auto pos = demuxer_->getSeekToPos(seekPos);
            demuxer_->seekToPos(pos);
        }
        start();
        LogI("seek to keyframe for {:.3f}s (strategy={}), epoch={}",
             targetSec,
             static_cast<int32_t>(strategy),
             seekContext_->currentEpoch());
    }
}

Time::TimePoint PlayPipeline::currentTime() noexcept {
    return clockSync_->masterTime(mediaInfo_.hasAudio);
}

Time::TimePoint PlayPipeline::audioTime() noexcept {
    return clockSync_->audioTime();
}

double PlayPipeline::cachedDuration() const noexcept {
    return demuxedDuration();
}

AVFrameRefPtr PlayPipeline::requestVideoFrame() noexcept {
    recordVsyncInterval();

    if (pipelineState_.load(std::memory_order_acquire) == PipelineState::Faulted) {
        return nullptr;
    }

    const double playbackRate = clockSync_->playbackRate();
    const double diff =
        clockSync_->masterTime(mediaInfo_.hasAudio).second() - clockSync_->videoTime().second();
    if (shouldHoldVideoFrame(diff, playbackRate)) {
        return nullptr;
    }

    auto frame = popVideoFrame();
    if (!frame) {
        if (seekContext_->isSeekPending()) {
            auto preview = seekPreviewFrame();
            if (preview) {
                return preview;
            }
        }
        bufferMonitor_->onStarvation(!config_.isLiveSource && config_.startBufferDuration == 0.0);
        return nullptr;
    }

    // seek 期间跳过 isDiscard 帧，直到取到 pts >= target 的正常帧
    if (seekContext_->isSeekPending()) {
        while (frame->isDiscard) {
            auto next = popVideoFrame();
            if (!next) {
                return nullptr;
            }
            frame = std::move(next);
        }
    }

    bufferMonitor_->resetStarvationCount();
    clockSync_->setVideoTime(Time::TimePoint::fromSeconds(frame->ptsTime()));

    // VSync 路径的首帧标记：CAS 保证只触发一次，与 push 路径互斥
    bool expected = false;
    if (readiness_.firstFrameRendered.compare_exchange_strong(expected,
                                                              true,
                                                              std::memory_order_acq_rel)) {
        if (auto session = playSession_.lock()) {
            session->markFirstFrame();
            SLARK_TRACE_MILESTONE(session.get(), "first_video_frame");
        }
        postToOwnerThread([this] {
            fireEvent(PipelineEvent::FirstFrameRendered);
        });
    }
    // seek 完成判断：取到第一个 pts >= target 的非 discard 帧时标记视频已 seek
    // isVideoSeeked 可能已由 push 路径标记，onSeekFrameConsumed 内部有 CAS 保护不会重复完成
    if (seekContext_->isSeekPending() && !frame->isDiscard) {
        onSeekFrameConsumed(TrackType::Video);
    }

    if (mediaInfo_.hasAudio && !seekContext_->isSeekPending()) {
        const double diffAfter =
            clockSync_->masterTime(mediaInfo_.hasAudio).second() - clockSync_->videoTime().second();
        chaseVideoFrames(frame, diffAfter, playbackRate);
    }

    return frame;
}

void PlayPipeline::recordVsyncInterval() noexcept {
    auto vsyncNow = std::chrono::steady_clock::now();
    // 记录两次 VSync 回调的间隔，而非函数耗时
    if (lastVsyncTime_ != std::chrono::steady_clock::time_point{}) {
        auto intervalUs =
            std::chrono::duration_cast<std::chrono::microseconds>(vsyncNow - lastVsyncTime_)
                .count();
        clockSync_->recordVsyncDuration(intervalUs);
    }
    lastVsyncTime_ = vsyncNow;
}

bool PlayPipeline::shouldHoldVideoFrame(double diff, double playbackRate) const noexcept {
    // 首帧未渲染前跳过 AV Sync Hold，保证快速推帧
    if (!readiness_.firstFrameRendered.load(std::memory_order_acquire)) {
        return false;
    }
    if (!mediaInfo_.hasAudio || seekContext_->isSeekPending()) {
        return false;
    }
    // 传入 kEarlyTolerance，消除 VSync 边界 ±2ms 抖动导致的误 Hold
    return ClockSync::evaluateAVSync(diff, playbackRate, ClockSync::kEarlyTolerance) ==
           AVSyncAction::Hold;
}

void PlayPipeline::chaseVideoFrames(AVFrameRefPtr& frame,
                                    double diff,
                                    double playbackRate) noexcept {
    // 用实测 VSync 间隔推算屏幕刷新率，fallback 到 60Hz
    const double videoFps = clockSync_->videoFps();
    const int64_t vsyncUs = clockSync_->metrics().vsyncDurationUs;
    constexpr double kFallbackHz = 60.0;
    const double displayHz = (vsyncUs > 0) ? (1e6 / static_cast<double>(vsyncUs)) : kFallbackHz;
    // 每个 VSync 窗口内视频帧数：120fps/60Hz=2，240fps/60Hz=4
    const int32_t framesPerVsync =
        (videoFps > 0.0 && displayHz > 0.0)
            ? std::max(1, static_cast<int32_t>(std::round(videoFps / displayHz)))
            : 1;
    // 每个 VSync 最多追 framesPerVsync-1 帧，至少保留 1 帧渲染
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

        // 达到本 VSync 窗口追帧上限，强制保留当前帧退出，避免画面跳跃
        if (chaseLimit > 0 && dropCount >= chaseLimit) {
            break;
        }
        diff =
            clockSync_->masterTime(mediaInfo_.hasAudio).second() - clockSync_->videoTime().second();
    }

    if (dropCount > 0) {
        clockSync_->recordDrop(dropCount);
        if (dropCount >= config_.maxDropPerVsync) {
            LogW("drop limit reached: dropped {} frames in one vsync", dropCount);
        }
    }
}

AVFrameRefPtr PlayPipeline::requestAudioFrame() noexcept {
    // 新架构中音频帧直接 push 到 audioRender，此接口不再被调用
    return nullptr;
}

double PlayPipeline::demuxedDuration() const noexcept {
    double maxPts = 0.0;
    videoFrames_.withLock([&maxPts](const auto& m) {
        if (!m.empty()) {
            maxPts = std::max(maxPts, m.rbegin()->second->ptsTime());
        }
    });
    // 纳入待解码队列中最新帧的 pts，确保背压水位覆盖 demux → decode 这一层缓冲
    pendingDecodeAudioQueue_.withLock([&maxPts](const auto& q) {
        if (!q.empty()) {
            maxPts = std::max(maxPts, q.back()->ptsTime());
        }
    });
    audioFrames_.withLock([&maxPts](const auto& q) {
        if (!q.empty()) {
            maxPts = std::max(maxPts, q.back()->ptsTime());
        }
    });
    pendingDecodeVideoQueue_.withLock([&maxPts](const auto& q) {
        if (!q.empty()) {
            maxPts = std::max(maxPts, q.back()->ptsTime());
        }
    });
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

void PlayPipeline::pauseDecoder() noexcept {
    if (audioDecoder_) {
        audioDecoder_->pause();
    }
    if (videoDecoder_) {
        videoDecoder_->pause();
    }
}

void PlayPipeline::resumeDecoder() noexcept {
    if (audioDecoder_) {
        audioDecoder_->start();
    }
    if (videoDecoder_) {
        videoDecoder_->start();
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
    fireEvent(event, std::move(error));
}

void PlayPipeline::onHeartbeat() noexcept {
    assert(isOnOwnerThread());
    if (bufferMonitor_) {
        bufferMonitor_->onHeartbeat();
    }
    pushAudioFrames();
    // seek 超时检测：5s 未完成则触发 SeekTimeout
    constexpr auto kSeekTimeout = std::chrono::milliseconds(5000);
    if (seekContext_->isSeekTimedOut(kSeekTimeout)) {
        std::ignore = seekContext_->completeSeek();
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

    // vsync 路径不阻塞，try_lock 失败直接跳过本帧
    videoFrames_.withTryLock([&frame, &popFrame](auto& m) {
        frame = popFrame(m);
    });

    if (!frame && !mediaInfo_.hasAudio && demuxer_ && demuxer_->isCompleted()) {
        // 纯视频流：视频帧耗尽且 demux 完成即为播放结束
        postToOwnerThread([this] {
            if (pipelineState_.load(std::memory_order_acquire) == PipelineState::Ended) {
                return;
            }
            pipelineState_.store(PipelineState::Ended, std::memory_order_release);
            fireEvent(PipelineEvent::RenderEnd);
        });
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

void PlayPipeline::setStreamControl(StreamControlFunc pauseStream,
                                    StreamControlFunc resumeStream) noexcept {
    pauseStreamFunc_ = std::move(pauseStream);
    resumeStreamFunc_ = std::move(resumeStream);
}

void PlayPipeline::setDemuxerComponent(std::shared_ptr<DemuxerComponent> demuxer) noexcept {
    demuxer_ = std::move(demuxer);
    connectComponents();
}

void PlayPipeline::onAudioDecoded(DecoderData data) noexcept {
    if (pipelineState_.load(std::memory_order_acquire) >= PipelineState::TearingDown) {
        return;
    }
    if (data.frame->isEOS) {
        return;
    }
    const uint64_t kFrameEpoch = seekContext_->currentEpoch();
    if (seekContext_->isEpochStale(kFrameEpoch)) {
        return;
    }
    audioFrames_.withLock([&data](auto& q) {
        q.emplace_back(std::move(data.frame));
    });
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
        clockSync_->setAudioTime(t);
    });
    audioRender_->setRenderEndCallback([this] {
        if (!demuxer_ || !demuxer_->isCompleted()) {
            return;
        }
        postToOwnerThread([this] {
            if (pipelineState_.load(std::memory_order_acquire) == PipelineState::Ended) {
                return;
            }
            pipelineState_.store(PipelineState::Ended, std::memory_order_release);
            fireEvent(PipelineEvent::RenderEnd);
        });
    });

    audioDecoder_ = std::make_shared<DecoderComponent>(DecoderComponentConfig{
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
        .dataProvider = [this]() -> DataProviderResult {
            return pendingDecodeAudioQueue_.withLock([](auto& q) -> DataProviderResult {
                if (q.empty()) {
                    return {nullptr, Error::Success};
                }
                auto frame = std::move(q.front());
                q.pop_front();
                return {std::move(frame), Error::Success};
            });
        },
    });

    openDecoderInternal(audioDecoder_, type, config);
    ++readiness_.expectedDecoders;
    registerComponents();
}

void PlayPipeline::onVideoDecoded(DecoderData data) noexcept {
    if (pipelineState_.load(std::memory_order_acquire) >= PipelineState::TearingDown) {
        return;
    }
    if (data.frame->isEOS) {
        return;
    }
    const uint64_t kFrameEpoch = seekContext_->currentEpoch();
    if (seekContext_->isEpochStale(kFrameEpoch)) {
        return;
    }
    if (data.frame->isDiscard) {
        // seek 期间 pts < target 的帧：入队供 seekPreviewFrame 抽帧预览
        videoFrames_.withLock([&data](auto& m) {
            m.emplace(data.frame->pts, std::move(data.frame));
        });
        return;
    }
    // FastForwardInGOP：首个 pts >= target 的帧到达，退出快进模式
    if (seekContext_->strategy() == SeekStrategy::FastForwardInGOP &&
        seekContext_->isSeekPending()) {
        exitFastForwardMode();
    }

    // seek 首帧：pts >= target 的第一帧通过 push 模式快速上屏，无需等待下一个 VSync
    // tryMarkVideoSeeked CAS 保证多线程下只有第一帧能进入 push 路径，后续帧直接入队走 VSync
    const bool isSeekFirstFrame =
        seekContext_->isSeekPending() && seekContext_->tryMarkVideoSeeked();
    if (isSeekFirstFrame || !readiness_.firstFrameRendered.load(std::memory_order_acquire)) {
        // push 模式：直接推帧渲染，不入队
        if (auto render = videoRender_.load()) {
            render->pushFrame(data.frame);
        }
        // 更新 video clock（原子操作，线程安全）
        clockSync_->setVideoTime(Time::TimePoint::fromSeconds(data.frame->ptsTime()));
        // 状态更新回到 ownerThread 处理，避免竞争
        const bool needFirstFrame = !readiness_.firstFrameRendered.load(std::memory_order_acquire);
        postToOwnerThread([this, needFirstFrame, isSeekFirstFrame] {
            if (needFirstFrame && !readiness_.firstFrameRendered.load(std::memory_order_acquire)) {
                readiness_.firstFrameRendered.store(true, std::memory_order_release);
                if (auto session = playSession_.lock()) {
                    session->markFirstFrame();
                    SLARK_TRACE_MILESTONE(session.get(), "first_video_frame");
                }
                fireEvent(PipelineEvent::FirstFrameRendered);
            }
            if (isSeekFirstFrame) {
                // isVideoSeeked 已在解码线程 CAS 标记，此处只做 completeSeek 判断
                onSeekFrameConsumed(TrackType::Video);
            }
        });
        return;
    }

    videoFrames_.withLock([&data](auto& m) {
        m.emplace(data.frame->pts, std::move(data.frame));
    });
}

void PlayPipeline::setupVideoTrack(DecoderType type,
                                   std::shared_ptr<VideoDecoderConfig> config) noexcept {
    SAssert(!videoDecoder_, "video track already set up");

    // 根据 SPS num_ref_frames 动态调整 video decoder watermark，避免 hardcode 导致的抖动
    if (config && config->numRefFrames > 0) {
        const int32_t refFrames = static_cast<int32_t>(config->numRefFrames);
        config_.videoDecoderLowWatermarkFrames = refFrames;
        config_.videoDecoderHighWatermarkFrames = std::max(refFrames * 2, refFrames + 4);
    }

    videoDecoder_ = std::make_shared<DecoderComponent>(DecoderComponentConfig{
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
        .dataProvider = [this]() -> DataProviderResult {
            return pendingDecodeVideoQueue_.withLock([](auto& q) -> DataProviderResult {
                if (q.empty()) {
                    return {nullptr, Error::Success};
                }
                auto frame = std::move(q.front());
                q.pop_front();
                return {std::move(frame), Error::Success};
            });
        },
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
    // 唤醒 ownerThread，避免等待下一个定时 tick
    if (wakeupCallback_) {
        wakeupCallback_();
    }
}

void PlayPipeline::fireEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept {
    assert(isOnOwnerThread() && "fireEvent must be called on ownerThread");
    // 必须携带 error 的事件类型
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
            const uint64_t kCurrentEpoch = seekContext_->currentEpoch();
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
    if (!audioDecoder_) {
        return false;
    }
    const auto seekTarget = seekContext_->targetSeekTime();
    bool enqueued = false;
    for (auto& frame : frames) {
        if (seekContext_->isEpochStale(epoch)) {
            break;
        }
        // seek 期间直接丢弃 pts < target 的音频帧，音频无参考帧依赖
        if (seekTarget && frame->ptsTime() < *seekTarget) {
            continue;
        }
        pendingDecodeAudioQueue_.withLock([&frame](auto& q) {
            q.emplace_back(std::shared_ptr<AVFrame>(std::move(frame)));
        });
        enqueued = true;
    }
    return enqueued;
}

bool PlayPipeline::dispatchVideoFrames(AVFramePtrArray& frames, uint64_t epoch) noexcept {
    if (!videoDecoder_) {
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

        // seek 期间 pts < target 的帧标记 isDiscard，仍送解码器维持参考链
        if (seekTarget && framePts < *seekTarget) {
            frame->isDiscard = true;
        }
        pendingDecodeVideoQueue_.withLock([&frame](auto& q) {
            q.emplace_back(std::shared_ptr<AVFrame>(std::move(frame)));
        });
        enqueued = true;
    }
    return enqueued;
}

void PlayPipeline::injectEosFrames() noexcept {
    if (audioDecoder_) {
        auto eos = std::make_shared<AVFrame>(AVFrameType::Audio);
        eos->isEOS = true;
        pendingDecodeAudioQueue_.withLock([&eos](auto& q) {
            q.emplace_back(std::move(eos));
        });
    }
    if (videoDecoder_) {
        auto eos = std::make_shared<AVFrame>(AVFrameType::Video);
        eos->isEOS = true;
        pendingDecodeVideoQueue_.withLock([&eos](auto& q) {
            q.emplace_back(std::move(eos));
        });
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
    }

    if (hasAudio && audioDecoder_) {
        audioDecoder_->scheduleDecoding();
    }
    if (hasVideo && videoDecoder_) {
        videoDecoder_->scheduleDecoding();
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
    const double kTarget = config_.demuxHighWatermarkDuration;
    const double kCurrentSec = clockSync_->masterTime(mediaInfo_.hasAudio).second();
    int32_t droppedTotal = 0;

    while (true) {
        const auto bounds = snapshotQueueBounds();
        if (std::max(0.0, bounds.maxPts - kCurrentSec) <= kTarget) {
            break;
        }
        // 截断点取两端最老帧的较大值，保证音视频 pts 对齐丢弃
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
    if (!audioRender_) {
        return;
    }
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
        if (!audioRender_->process(frame)) {
            audioFrames_.withLock([&frame](auto& q) {
                q.emplace_front(std::move(frame));
            });
            break;
        }
        // seek 完成判断：第一帧成功推入 audioRender 时标记音频已 seek，后续帧跳过
        if (seekContext_->isSeekPending() && !seekContext_->isAudioSeeked()) {
            onSeekFrameConsumed(TrackType::Audio);
        }
    }
}

void PlayPipeline::exitFastForwardMode() noexcept {
    // 恢复背压控制需回到 ownerThread，audioDecoder_->start() 非线程安全
    postToOwnerThread([this] {
        if (bufferMonitor_) {
            bufferMonitor_->resumeBackpressure();
        }
        if (audioDecoder_) {
            audioDecoder_->start();
        }
        LogI("fast-forward mode exited");
    });
}

AVFrameRefPtr PlayPipeline::seekPreviewFrame() noexcept {
    constexpr int32_t kPreviewFrameStep = 3;
    AVFrameRefPtr previewFrame;

    videoFrames_.withTryLock([&](auto& m) {
        if (m.empty()) {
            return;
        }
        const auto strategy = seekContext_->strategy();
        if (strategy == SeekStrategy::FastForwardInGOP) {
            // GOP 内快进：每 kPreviewFrameStep 帧取一帧渲染，消费掉头部旧帧
            int32_t idx = 0;
            auto it = m.begin();
            while (it != m.end()) {
                if (idx % kPreviewFrameStep == 0) {
                    previewFrame = it->second;
                }
                ++idx;
                it = m.erase(it);
            }
        } else {
            // SeekToKeyframe / SeekToGOPKeyframe：取最新的 isDiscard 关键帧预览
            // 消费掉该关键帧之前（含）的所有旧帧
            for (auto it = m.rbegin(); it != m.rend(); ++it) {
                const auto& f = it->second;
                if (f && f->isDiscard && f->info && f->info->isKeyFrame()) {
                    previewFrame = f;
                    m.erase(m.begin(), it.base());
                    break;
                }
            }
        }
    });
    return previewFrame;
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

} // namespace slark
