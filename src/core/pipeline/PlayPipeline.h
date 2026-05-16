//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AVFrame.hpp"
#include "AudioInfo.h"
#include "BufferMonitor.h"
#include "ClockSync.h"
#include "Data.h"
#include "DecoderComponent.h"
#include "DecoderConfig.h"
#include "IDemuxer.h"
#include "IPipelineComponent.h"
#include "MediaDefs.h"
#include "NonCopyable.h"
#include "PipelineConfig.h"
#include "PipelineError.h"
#include "PipelineEvent.h"
#include "SeekContext.h"
#include "Synchronized.hpp"
#include "Time.hpp"
#include "perf/PlaySession.h"

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <thread>

namespace slark {

using StreamControlFunc = std::function<void()>;

class DemuxerComponent;
class DecoderComponent;
class AudioRenderComponent;
class IDemuxer;
struct IVideoRender;

struct MediaInfo {
    bool hasAudio = false;
    bool hasVideo = false;
    CTime totalDuration;
};

enum class PipelineState : uint8_t {
    Idle,
    Built,
    Playing,
    Paused,
    TearingDown, // teardown 已发起，等待组件退出
    Ended,       // 正常播放结束（RenderEnd 已触发）
    Faulted,     // 不可恢复错误，终态
};

struct ReadinessTracker {
    int32_t expectedDecoders = 0;
    int32_t readyDecoders = 0;
    bool liveVideoKeyframeSeen = false;
    bool liveAudioPacketSeen = false;
    bool readyFired = false;
    std::atomic<bool> firstFrameRendered{false}; ///< 首帧已渲染，之后才启用 AV Sync 和背压 Hold
};

class PlayPipeline : public NonCopyMovable, public IBufferMonitorDelegate {
public:
    explicit PlayPipeline(BackpressureConfig config) noexcept;

    ~PlayPipeline();

    void build() noexcept;

    void start() noexcept;

    void pause() noexcept;

    void flush() noexcept;

    void teardown() noexcept;

    void seek(CTime target, bool isAccurate = true) noexcept;

    void applyLiveConfig() noexcept;

    [[nodiscard]] Time::TimePoint currentTime() noexcept;

    [[nodiscard]] Time::TimePoint audioTime() noexcept;

    [[nodiscard]] double cachedDuration() const noexcept;

    [[nodiscard]] AVFrameRefPtr requestVideoFrame() noexcept;

    [[nodiscard]] AVFrameRefPtr requestAudioFrame() noexcept;

    void drainPendingTasks() noexcept;

    void onHeartbeat() noexcept;

    void setStreamControl(StreamControlFunc pauseStream, StreamControlFunc resumeStream) noexcept;

    void setPlaySession(std::weak_ptr<perf::PlaySession> session) noexcept {
        playSession_ = std::move(session);
    }

    void setEventCallback(PipelineEventCallback callback) noexcept {
        onEvent_ = std::move(callback);
    }

    void setWakeupCallback(std::function<void()> callback) noexcept {
        wakeupCallback_ = std::move(callback);
    }

    [[nodiscard]] const MediaInfo& mediaInfo() const noexcept {
        return mediaInfo_;
    }

    [[nodiscard]] bool isFaulted() const noexcept {
        return pipelineState_.load(std::memory_order_acquire) == PipelineState::Faulted;
    }

    [[nodiscard]] bool isBuilt() const noexcept {
        const auto s = pipelineState_.load(std::memory_order_acquire);
        return s != PipelineState::Idle;
    }

    [[nodiscard]] bool isOnOwnerThread() const noexcept;

    void setVideoRender(std::weak_ptr<IVideoRender> render) noexcept;

    void setDemuxerComponent(std::shared_ptr<DemuxerComponent> demuxer) noexcept;

    [[nodiscard]] std::shared_ptr<DemuxerComponent> demuxer() const noexcept {
        return demuxer_;
    }

    [[nodiscard]] std::shared_ptr<IVideoRender> videoRender() const noexcept {
        return videoRender_.load();
    }

    void setupAudioTrack(DecoderType type,
                         std::shared_ptr<AudioDecoderConfig> config,
                         std::shared_ptr<AudioInfo> audioInfo,
                         float volume) noexcept;

    /// header 解析完成后调用，pipeline 内部创建 video decoder。
    void setupVideoTrack(DecoderType type, std::shared_ptr<VideoDecoderConfig> config) noexcept;

    void setVideoFps(double fps) noexcept {
        clockSync_->setVideoFps(fps);
    }

    void setVolume(float volume) noexcept;

private:
    [[nodiscard]] double demuxedDuration() const noexcept override;

    void pauseStream() noexcept override;

    void resumeStream() noexcept override;

    [[nodiscard]] int32_t audioDecodedFrameCount() const noexcept override;

    [[nodiscard]] int32_t videoDecodedFrameCount() const noexcept override;

    void pauseAudioDecoder() noexcept override;

    void resumeAudioDecoder() noexcept override;

    void pauseVideoDecoder() noexcept override;

    void resumeVideoDecoder() noexcept override;

    void onPrebufferComplete() noexcept override;

    void onDropOldest() noexcept override;

    void onBufferEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept override;

    void postToOwnerThread(std::function<void()> task) noexcept;

    void fireEvent(PipelineEvent event, std::optional<PipelineError> error = std::nullopt) noexcept;

    void firePipelineReady() noexcept;

    void checkAndFirePipelineReady() noexcept;

    [[nodiscard]] bool checkLiveReadyCondition() const noexcept;

    void connectComponents() noexcept;

    void handleDemuxHeader(const std::shared_ptr<IDemuxer>& demuxer) noexcept;

    void dispatchFrames(DemuxerResult&& result, uint64_t epoch) noexcept;

    bool dispatchAudioFrames(AVFramePtrArray& frames, uint64_t epoch) noexcept;

    bool dispatchVideoFrames(AVFramePtrArray& frames, uint64_t epoch) noexcept;

    void injectEosFrames() noexcept;

    void notifyDataEnqueued() noexcept;

    void stopComponents() noexcept;

    [[nodiscard]] bool joinWithTimeout(std::chrono::milliseconds timeout) noexcept;

    void enterFaultedState() noexcept;

    void openDecoderInternal(std::shared_ptr<DecoderComponent> decoder,
                             DecoderType type,
                             const std::shared_ptr<DecoderConfig>& config) noexcept;

    void onAudioDecoded(DecoderData data) noexcept;

    void onVideoDecoded(DecoderData data) noexcept;

    [[nodiscard]] SeekStrategy resolveSeekStrategy(double targetSec) const noexcept;

    void onSeekFrameConsumed(TrackType track) noexcept;

    void exitFastForwardMode() noexcept;

    [[nodiscard]] AVFrameRefPtr seekPreviewFrame() noexcept;

    [[nodiscard]] AVFrameRefPtr popVideoFrame() noexcept;

    void dropOldestFrames() noexcept;

    void pushAudioFrames() noexcept;

    struct QueueBounds {
        double audioOldestPts = -1.0;
        double videoOldestPts = -1.0;
        double maxPts = 0.0;
    };

    [[nodiscard]] QueueBounds snapshotQueueBounds() noexcept;

    int32_t dropFramesUpTo(double cutoffPts) noexcept;

    void recordVsyncInterval() noexcept;

    [[nodiscard]] bool shouldHoldVideoFrame(double diff, double playbackRate) const noexcept;

    void chaseVideoFrames(AVFrameRefPtr& frame, double diff, double playbackRate) noexcept;

    void updateLiveReadyFlags(const DemuxerResult& result) noexcept;

    void registerComponents() noexcept;

private:
    BackpressureConfig config_;
    std::thread::id ownerThreadId_;
    PipelineEventCallback onEvent_;
    MediaInfo mediaInfo_;

    Synchronized<std::deque<std::function<void()>>> pendingTasks_;
    Synchronized<std::map<int64_t, AVFrameRefPtr>> videoFrames_;
    Synchronized<std::deque<AVFrameRefPtr>> audioFrames_;

    // Channel senders：替代旧的 pendingDecodeXxxQueue_
    SenderPtr<AVFrameRefPtr> audioFrameSender_;
    SenderPtr<AVFrameRefPtr> videoFrameSender_;

    // 累加器：记录最后入队帧的 pts，用于 demuxedDuration 统计
    std::atomic<double> lastEnqueuedAudioPts_{0.0};
    std::atomic<double> lastEnqueuedVideoPts_{0.0};

    std::shared_ptr<DemuxerComponent> demuxer_;
    std::shared_ptr<DecoderComponent> audioDecoder_;
    std::shared_ptr<DecoderComponent> videoDecoder_;
    std::shared_ptr<AudioRenderComponent> audioRender_;
    AtomicWeakPtr<IVideoRender> videoRender_;

    std::unique_ptr<ClockSync> clockSync_;
    std::unique_ptr<SeekContext> seekContext_;
    std::unique_ptr<BufferMonitor> bufferMonitor_;

    StreamControlFunc pauseStreamFunc_;
    StreamControlFunc resumeStreamFunc_;
    std::function<void()> wakeupCallback_;

    std::weak_ptr<perf::PlaySession> playSession_;

    std::atomic<PipelineState> pipelineState_{PipelineState::Idle};

    ReadinessTracker readiness_;

    // dataEnqueued 去抖标志，仅 ownerThread 消费，decoder 线程写
    std::atomic<bool> dataEnqueuedPending_{false};

    std::vector<std::shared_ptr<IPipelineComponent>> components_;

    std::chrono::steady_clock::time_point lastVsyncTime_{};
};

} // namespace slark
