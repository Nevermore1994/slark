//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AVFrame.hpp"
#include "BufferMonitor.h"
#include "ClockSync.h"
#include "Data.h"
#include "DecoderComponent.h"
#include "IPipelineComponent.h"
#include "PipelineConfig.h"
#include "PipelineError.h"
#include "PipelineEvent.h"
#include "SeekCoordinator.h"
#include "Synchronized.hpp"
#include "Time.hpp"
#include "VideoFrameContainer.h"
#include "perf/PlaySession.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace slark {

class DemuxerComponent;
class DecoderComponent;
class AudioRenderComponent;
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
 * 线程安全约定：
 *  - 所有公开 API 必须在 ownerThread 调用
 *  - onEvent 回调保证在 ownerThread 触发
 *  - 内部跨线程投递统一通过 postToOwnerThread() 完成
 */
enum class PipelineState : uint8_t {
    Idle,
    Built,
    Playing,
    Paused,
    Faulted,
};

class PlayPipeline : public NonCopyMovable {
public:
    explicit PlayPipeline(BackpressureConfig config) noexcept;
    ~PlayPipeline() override;

    void build() noexcept;

    void start() noexcept;
    void pause() noexcept;
    void flush() noexcept;

    /**
     * @brief 有界收敛销毁：置位 teardownFlag_ → 停止组件 → 有界 join(2s) → 超时进入 Faulted。
     */
    void teardown() noexcept;

    /**
     * @brief seek 到指定时间，递增 seekEpoch，flush 队列，seek 完成后触发 SeekCompleted。
     */
    void seek(CTime target) noexcept;

    /**
     * @brief HLS live 识别后调用，切换为 live 背压配置并重建 BufferMonitor。
     * 必须在 ownerThread 调用，build() 前后均可。
     */
    void applyLiveConfig() noexcept;

    [[nodiscard]] Time::TimePoint currentTime() noexcept;
    [[nodiscard]] AVFrameRefPtr requestVideoFrame() noexcept;
    [[nodiscard]] AVFrameRefPtr requestAudioFrame() noexcept;

    [[nodiscard]] double cachedDuration() const noexcept {
        return demuxedDuration();
    }

    /**
     * @brief 消费 ownerThread 任务队列，必须在 ownerThread 的事件循环中调用。
     */
    void drainPendingTasks() noexcept;

    // BufferMonitor 低频心跳，必须在 ownerThread 调用
    void onHeartbeat() noexcept;

    /**
     * @brief 设置流控回调，供 BufferMonitor 控制 IStream 的 pause/resume。
     * 必须在 build() 之前调用。
     */
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
    void setAudioRender(std::shared_ptr<AudioRenderComponent> render) noexcept;
    void setDemuxerComponent(std::shared_ptr<DemuxerComponent> demuxer) noexcept;

    [[nodiscard]] std::shared_ptr<DemuxerComponent> demuxer() const noexcept {
        return demuxer_;
    }

    [[nodiscard]] std::shared_ptr<DecoderComponent> audioDecoder() const noexcept {
        return audioDecoder_;
    }

    [[nodiscard]] std::shared_ptr<DecoderComponent> videoDecoder() const noexcept {
        return videoDecoder_;
    }

    [[nodiscard]] std::shared_ptr<AudioRenderComponent> audioRender() const noexcept {
        return audioRender_;
    }

    [[nodiscard]] std::shared_ptr<IVideoRender> videoRender() const noexcept {
        return videoRender_.load();
    }

    /**
     * @brief 创建 decoder 并绑定 receive callback，必须在 build() 之前调用。
     */
    void createAudioDecoder() noexcept;
    void createVideoDecoder() noexcept;

    /**
     * @brief 异步 open decoder，成功后累加 decoderReadyCount_ 并检查 PipelineReady。
     */
    void openDecoder(std::shared_ptr<DecoderComponent> decoder,
                     DecoderType type,
                     const std::shared_ptr<DecoderConfig>& config,
                     bool isVideo) noexcept;

private:
    void postToOwnerThread(std::function<void()> task) noexcept;
    void fireEvent(PipelineEvent event, std::optional<PipelineError> error = std::nullopt) noexcept;
    void checkAndFirePipelineReady() noexcept;
    [[nodiscard]] double demuxedDuration() const noexcept;
    void initBufferMonitor() noexcept;
    [[nodiscard]] bool checkLiveReadyCondition() const noexcept;
    void connectComponents() noexcept;
    void registerComponents() noexcept;
    void stopComponents() noexcept;
    [[nodiscard]] bool joinWithTimeout(std::chrono::milliseconds timeout) noexcept;
    void onBFrameDetected() noexcept;
    void enterFaultedState() noexcept;
    void reportFaultedError() noexcept;

    // seek 完成检查：轨道帧消费后标记对应 seek 完成，全部轨道完成时触发事件
    void onSeekFrameConsumed(bool isVideo) noexcept;
    [[nodiscard]] AVFrameRefPtr popVideoFrame() noexcept;
    // hard-cap 触发时主动丢最旧帧，收敛到 highWatermark 以下
    void dropOldestFrames() noexcept;

private:
    BackpressureConfig config_;
    std::thread::id ownerThreadId_;
    PipelineEventCallback onEvent_;
    MediaInfo mediaInfo_;

    Synchronized<std::deque<std::function<void()>>> pendingTasks_;

    Synchronized<std::deque<AVFrameRefPtr>> audioFrames_;
    Synchronized<VideoFrameContainer> videoFrames_;

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
    std::atomic<bool> bFrameDetected_{false};
    std::atomic<bool> renderEndFired_{false};
    std::atomic<int32_t> consecutiveTryLockFailures_{0};
};

} // namespace slark
namespace slark
