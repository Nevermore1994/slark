//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <thread>

#include "AVFrame.hpp"
#include "AudioInfo.h"
#include "BufferingTracker.h"
#include "ClockSync.h"
#include "Data.h"
#include "DecoderComponent.h"
#include "DecoderConfig.h"
#include "IDemuxer.h"
#include "IPipelineComponent.h"
#include "IStream.h"
#include "MediaDefs.h"
#include "MediaSourceType.h"
#include "NonCopyable.h"
#include "FlowConfig.h"
#include "PipelineTypes.h"
#include "PlayerTypes.h"
#include "SeekContext.h"
#include "SourceFlowController.h"
#include "Synchronized.hpp"
#include "Time.hpp"
#include "perf/PlaySession.h"

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
    StreamType streamType = StreamType::File;
};

struct ReadinessTracker {
    int32_t expectedDecoders = 0;
    int32_t readyDecoders = 0;
    bool liveVideoKeyframeSeen = false;
    bool liveAudioPacketSeen = false;
    bool readyFired = false;
    std::atomic<bool> firstFrameRendered{false};
};

class PlayPipeline : public NonCopyMovable,
                     public ISourceFlowDelegate,
                     public IBufferingTrackerDelegate {
 public:
    PlayPipeline(FlowConfig config, pipeline::MediaSourceType sourceType) noexcept;

    ~PlayPipeline();

    void startDemuxing() noexcept;

    void start() noexcept;

    void pause() noexcept;

    void flush() noexcept;

    [[nodiscard]] bool teardown() noexcept;

    void seek(CTime target, bool isAccurate = true) noexcept;

    [[nodiscard]] Time::TimePoint currentTime() noexcept;

    [[nodiscard]] Time::TimePoint audioTime() noexcept;

    [[nodiscard]] Time::TimePoint videoTime() noexcept {
        return clockSync_->videoTime();
    }

    [[nodiscard]] bool canFetchMore() const noexcept;

    [[nodiscard]] double cacheEndTime() const noexcept;

    [[nodiscard]] AVFrameRefPtr requestVideoFrame() noexcept;

    void drainPendingTasks() noexcept;

    // High-frequency tick (playerThreadTickMs): backpressure regulation + audio frame pushing
    void runFastTick(bool isPlaying) noexcept;

    void setStreamControl(StreamControlFunc interruptStream,
                          StreamControlFunc resumeStream) noexcept;

    void setCloseFetchGateCallback(StreamControlFunc callback) noexcept {
        closeFetchGateFunc_ = std::move(callback);
    }

    void setOpenFetchGateCallback(StreamControlFunc callback) noexcept {
        openFetchGateFunc_ = std::move(callback);
    }

    void setSeekByTimeFunc(std::function<StreamResult<void>(double, uint64_t)> func) noexcept {
        seekByTimeFunc_ = std::move(func);
    }

    void setStreamType(StreamType type) noexcept {
        mediaInfo_.streamType = type;
    }

    void setPlaybackRange(double start, double duration, bool enabled) noexcept;

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

    [[nodiscard]] bool isStarted() const noexcept {
        return componentStarted_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isOnOwnerThread() const noexcept;

    void bindToCurrentThread() noexcept {
        ownerThreadId_ = std::this_thread::get_id();
    }

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

    /// Called after the header is parsed; the pipeline creates the video decoder internally.
    void setupVideoTrack(DecoderType type, std::shared_ptr<VideoDecoderConfig> config) noexcept;

    void setVideoFps(double fps) noexcept {
        clockSync_->setVideoFps(fps);
    }

    void setVolume(float volume) noexcept;

    void tryFirePlaybackComplete() noexcept;

    [[nodiscard]] uint64_t nextSeekEpoch() const noexcept {
        return seekContext_->currentEpoch() + 1;
    }

    [[nodiscard]] uint64_t currentSeekEpoch() const noexcept {
        return seekContext_->currentEpoch();
    }

    [[nodiscard]] bool isSeekPending() const noexcept {
        return seekContext_->isSeekPending();
    }

 private:
    [[nodiscard]] double demuxedDuration() const noexcept;

    [[nodiscard]] double demuxedFrontierSec() const noexcept;

    void interruptStream() noexcept override;

    void resumeStream() noexcept override;
    void closeFetchGate() noexcept override;
    void openFetchGate() noexcept override;

    [[nodiscard]] int32_t audioDecodedFrameCount() const noexcept;

    [[nodiscard]] int32_t videoDecodedFrameCount() const noexcept;

    void onPrebufferComplete() noexcept override;

    void onBufferEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept override;

    void postToOwnerThread(std::function<void()> task) noexcept;

    void fireEvent(PipelineEvent event, std::optional<PipelineError> error = std::nullopt) noexcept;

    void firePipelineReady() noexcept;

    void tryFirePipelineReady() noexcept;

    [[nodiscard]] bool checkLiveReadyCondition() const noexcept;

    void connectComponents() noexcept;

    void handleDemuxHeader(const std::shared_ptr<IDemuxer>& demuxer) noexcept;

    void dispatchFrames(DemuxerResult&& result, uint64_t epoch) noexcept;

    bool dispatchAudioFrames(AVFramePtrArray& frames, uint64_t epoch) noexcept;

    bool dispatchVideoFrames(AVFramePtrArray& frames, uint64_t epoch) noexcept;

    void injectEosFrames(uint64_t epoch) noexcept;

    void runBufferControl() noexcept;

    void logBufferDiagnostics(double bufferedSec) noexcept;

    void stopComponents() noexcept;

    [[nodiscard]] bool joinWithTimeout(std::chrono::milliseconds timeout) noexcept;

    void openDecoderInternal(std::shared_ptr<DecoderComponent> decoder,
                             DecoderType type,
                             const std::shared_ptr<DecoderConfig>& config) noexcept;

    void onAudioDecoded(DecoderData data) noexcept;

    void onVideoDecoded(DecoderData data) noexcept;

    [[nodiscard]] bool shouldDiscardFrame(const DecoderData& data) noexcept;

    void handleFirstVideoFrame(const AVFrameRefPtr& frame) noexcept;

    void handleSeekFrameConsumed(TrackType track) noexcept;

    /// FastForwardInGOP: no demuxer relocation or decoder flush; fast-forward consuming
    /// [current, target) audio/video
    void fastForwardInGOP(CTime target) noexcept;

    /// SeekToKeyframe: pause-flush-relocate-resume
    void performKeyframeSeek(CTime target) noexcept;

    [[nodiscard]] AVFrameRefPtr popVideoFrame() noexcept;

    void pushAudioFrames(bool isPlaying) noexcept;
    void pushSeekFrames() noexcept;

    void recordVsyncInterval() noexcept;

    [[nodiscard]] bool shouldHoldVideoFrame(double diff, double playbackRate) const noexcept;

    [[nodiscard]] bool isDecodeCompleted() const noexcept;

    void chaseVideoFrames(AVFrameRefPtr& frame, double diff, double playbackRate) noexcept;

    void handleSeekFrameStarvation() noexcept;

    [[nodiscard]] AVFrameRefPtr skipDiscardFramesDuringSeek(AVFrameRefPtr frame) noexcept;

    void markFirstFrameRenderedIfNeeded() noexcept;

    [[nodiscard]] bool isAudioReadyForPlaybackEnd() const noexcept;

    [[nodiscard]] bool isVideoReadyForPlaybackEnd() const noexcept;

    [[nodiscard]] bool hasPendingFrames() const noexcept;

    [[nodiscard]] bool isBeforePlaybackRange(double pts) const noexcept;

    [[nodiscard]] bool isAfterPlaybackRange(double pts) const noexcept;

    [[nodiscard]] bool hasReachedPlaybackRangeEnd() const noexcept;

    void markSeekedTracksIfDecodersCompleted() noexcept;

    [[nodiscard]] AVFrameRefPtr skipFramesBeforeTarget(AVFrameRefPtr frame, double target) noexcept;

    void updateLiveReadyFlags(const DemuxerResult& result) noexcept;

    [[nodiscard]] bool canFastForward(double targetSec) const noexcept;

    void registerComponents() noexcept;

 private:
    FlowConfig config_;
    const bool kIsLiveSource;
    std::thread::id ownerThreadId_;
    PipelineEventCallback onEvent_;
    MediaInfo mediaInfo_;

    Synchronized<std::deque<std::function<void()>>> pendingTasks_;
    Synchronized<std::map<int64_t, AVFrameRefPtr>> videoFrames_;
    Synchronized<std::deque<AVFrameRefPtr>> audioFrames_;

    SenderPtr<AVFrameRefPtr> audioFrameSender_;
    SenderPtr<AVFrameRefPtr> videoFrameSender_;
    ReceiverPtr<AVFrameRefPtr> audioFrameReceiver_;
    ReceiverPtr<AVFrameRefPtr> videoFrameReceiver_;

    std::atomic<double> lastEnqueuedAudioPts_{0.0};
    std::atomic<double> lastEnqueuedVideoPts_{0.0};
    std::atomic<bool> eosInjected_{false};

    std::shared_ptr<DemuxerComponent> demuxer_;
    std::shared_ptr<DecoderComponent> audioDecoder_;
    std::shared_ptr<DecoderComponent> videoDecoder_;
    std::shared_ptr<AudioRenderComponent> audioRender_;
    AtomicWeakPtr<IVideoRender> videoRender_;

    std::unique_ptr<ClockSync> clockSync_;
    std::unique_ptr<SeekContext> seekContext_;
    std::unique_ptr<SourceFlowController> sourceFlow_;
    std::unique_ptr<BufferingTracker> bufferingTracker_;

    StreamControlFunc interruptStreamFunc_;
    StreamControlFunc resumeStreamFunc_;
    StreamControlFunc closeFetchGateFunc_;
    StreamControlFunc openFetchGateFunc_;
    std::function<void()> wakeupCallback_;
    std::function<StreamResult<void>(double, uint64_t)> seekByTimeFunc_;

    std::weak_ptr<perf::PlaySession> playSession_;

    std::atomic<bool> tearingDown_{false};
    std::atomic<bool> completionPending_{false};
    std::atomic<bool> componentStarted_{false};

    double playbackRangeStart_ = 0.0;
    double playbackRangeEnd_ = 0.0;
    bool playbackRangeEnabled_ = false;

    ReadinessTracker readiness_;

    std::vector<std::shared_ptr<IPipelineComponent>> components_;

    std::chrono::steady_clock::time_point lastVsyncTime_{};
    std::chrono::steady_clock::time_point lastDiagnosticsTime_{};
};

}  // namespace slark
