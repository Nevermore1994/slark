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
#include "IPipelineComponent.h"
#include "NonCopyable.h"
#include "PipelineConfig.h"
#include "PipelineError.h"
#include "PipelineEvent.h"
#include "SeekCoordinator.h"
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
#include <vector>

namespace slark {

class DemuxerComponent;
class DecoderComponent;
class AudioRenderComponent;
struct IDemuxer;
struct IVideoRender;

struct MediaInfo {
    bool hasAudio = false;
    bool hasVideo = false;
    double fps = 0.0;
    CTime totalDuration;
};

/**
 * @brief 播放管线核心协调器。
 *
 * 线程安全约定：所有公开 API 必须在 ownerThread 调用，onEvent 回调保证在 ownerThread 触发。
 */
enum class PipelineState : uint8_t {
    Idle,
    Built,
    Playing,
    Paused,
    Faulted,
};

class PlayPipeline : public NonCopyMovable, public IBufferMonitorDelegate {
public:
    explicit PlayPipeline(BackpressureConfig config) noexcept;

    ~PlayPipeline() override;

    void build() noexcept;

    void start() noexcept;

    void pause() noexcept;

    void flush() noexcept;

    void teardown() noexcept;

    void seek(CTime target) noexcept;

    void applyLiveConfig() noexcept;

    [[nodiscard]] Time::TimePoint currentTime() noexcept;

    [[nodiscard]] AVFrameRefPtr requestVideoFrame() noexcept;

    [[nodiscard]] AVFrameRefPtr requestAudioFrame() noexcept;

    [[nodiscard]] double cachedDuration() const noexcept {
        return demuxedDuration();
    }

    void drainPendingTasks() noexcept;

    void onHeartbeat() noexcept;

    [[nodiscard]] double demuxedDuration() const noexcept override;

    void pauseStream() noexcept override;

    void resumeStream() noexcept override;

    void onPrebufferComplete() noexcept override;

    void onDropOldest() noexcept override;

    void onBufferEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept override;

    void setStreamControl(StreamControlFunc pauseStream, StreamControlFunc resumeStream) noexcept;

    void setPlaySession(std::weak_ptr<perf::PlaySession> session) noexcept {
        playSession_ = std::move(session);
    }

    void setEventCallback(PipelineEventCallback callback) noexcept {
        onEvent_ = std::move(callback);
    }

    [[nodiscard]] const MediaInfo& mediaInfo() const noexcept {
        return mediaInfo_;
    }

    [[nodiscard]] bool isFaulted() const noexcept {
        return isolated_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isBuilt() const noexcept {
        return ownerThreadId_ != std::thread::id{};
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

    /**
     * @brief header 解析完成后调用，pipeline 内部创建 audio decoder + audio render。
     * @param volume 初始音量 [0.0, 1.0]
     */
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

    [[nodiscard]] Time::TimePoint audioPlayedTime() noexcept;

private:
    void postToOwnerThread(std::function<void()> task) noexcept;

    void fireEvent(PipelineEvent event, std::optional<PipelineError> error = std::nullopt) noexcept;

    void firePipelineReady() noexcept;

    void checkAndFirePipelineReady() noexcept;

    void initBufferMonitor() noexcept;

    [[nodiscard]] bool checkLiveReadyCondition() const noexcept;

    void connectComponents() noexcept;

    void handleDemuxHeader(const std::shared_ptr<IDemuxer>& demuxer) noexcept;

    void dispatchFrames(DemuxerResult&& result, uint64_t epoch) noexcept;

    void notifyDataEnqueued() noexcept;

    void registerComponents() noexcept;

    void stopComponents() noexcept;

    [[nodiscard]] bool joinWithTimeout(std::chrono::milliseconds timeout) noexcept;

    void enterFaultedState() noexcept;

    void reportFaultedError() noexcept;

    void openDecoderInternal(std::shared_ptr<DecoderComponent> decoder,
                             DecoderType type,
                             const std::shared_ptr<DecoderConfig>& config,
                             bool isVideo) noexcept;

    void onSeekFrameConsumed(bool isVideo) noexcept;

    [[nodiscard]] AVFrameRefPtr popVideoFrame() noexcept;

    void dropOldestFrames() noexcept;

    // 返回音频主时钟：有音频时用硬件实际播出时间，无音频时用视频时钟
    [[nodiscard]] Time::TimePoint audioMasterTime() const noexcept;

private:
    BackpressureConfig config_;
    std::thread::id ownerThreadId_;
    PipelineEventCallback onEvent_;
    MediaInfo mediaInfo_;

    Synchronized<std::deque<std::function<void()>>> pendingTasks_;
    Synchronized<std::deque<AVFrameRefPtr>> audioFrames_;
    Synchronized<std::map<int64_t, AVFrameRefPtr>> videoFrames_;

    std::shared_ptr<DemuxerComponent> demuxer_;
    std::shared_ptr<DecoderComponent> audioDecoder_;
    std::shared_ptr<DecoderComponent> videoDecoder_;
    std::shared_ptr<AudioRenderComponent> audioRender_;
    AtomicWeakPtr<IVideoRender> videoRender_;

    std::vector<std::shared_ptr<IPipelineComponent>> components_;

    std::unique_ptr<ClockSync> clockSync_;
    std::unique_ptr<SeekCoordinator> seekCoordinator_;
    std::unique_ptr<BufferMonitor> bufferMonitor_;

    StreamControlFunc pauseStreamFunc_;
    StreamControlFunc resumeStreamFunc_;

    std::weak_ptr<perf::PlaySession> playSession_;

    PipelineState pipelineState_{PipelineState::Idle};
    std::atomic<bool> teardownFlag_{false};
    std::atomic<bool> isolated_{false};
    std::atomic<bool> readyFired_{false};
    std::atomic<int32_t> decoderReadyCount_{0};

    std::atomic<bool> liveHasFirstVideoKeyframe_{false};
    std::atomic<bool> liveHasFirstAudioPacket_{false};
    std::atomic<bool> renderEndFired_{false};
    std::atomic<bool> dataEnqueuedPending_{false};
    std::atomic<int32_t> consecutiveTryLockFailures_{0};
    std::chrono::steady_clock::time_point lastVsyncTime_{};
};

} // namespace slark
