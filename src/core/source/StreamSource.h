//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "FlowConfig.h"
#include "IDemuxer.h"
#include "IStream.h"
#include "EngineTypes.h"
#include "Scheduler.h"
#include "Source.h"
#include "WatermarkGate.h"

namespace slark::engine {

struct SourceOptions {
    std::string uri;
    Range range;
    FlowConfig flow;
    std::string perfSessionId;
    DemuxerType demuxer = DemuxerType::Unknown;

    PlaybackRange playbackRange;

    // Replaces the DemuxerManager lookup. Tests use it to feed scripted frames.
    std::function<std::shared_ptr<IDemuxer>()> demuxerFactory;
};

// Subclass destructors must call close(): the loader still calls their virtuals.
class StreamSource : public Source {
 public:
    using Wake = std::function<void()>;

    ~StreamSource() override;

    [[nodiscard]] MediaInfo info() const override;
    [[nodiscard]] std::shared_ptr<AudioInfo> audioTrackInfo() const override;
    [[nodiscard]] std::shared_ptr<VideoInfo> videoTrackInfo() const override;
    [[nodiscard]] Time::TimePoint cacheEnd() const override;
    [[nodiscard]] bool isLoading() const override;
    [[nodiscard]] std::optional<Error> failure() const override;
    [[nodiscard]] ErrorDetail failureDetail() const override;
    [[nodiscard]] Receiver<AVFrameRefPtr>* frames(TrackType type) override;
    [[nodiscard]] DebugInfo debugInfo() const override;

    void seekTo(Time::TimePoint target, SeekEpoch epoch) override;    bool retarget(Time::TimePoint target) override;
    void regulate(Time::TimePoint clockNow) override;

    void setSeeking(bool seeking) noexcept override;

    void stopLoading() override;
    void close() override;

 protected:
    StreamSource(SourceOptions options, SeekEpoch epoch, Wake wake);

    // Call at the end of the most derived constructor: the loader starts calling virtuals.
    void start();

    // Runs on the loader thread. Returns the stream to read from.
    virtual StreamResult<std::shared_ptr<IStream>> createStream() = 0;
    virtual StreamResult<void> seekStream(Time::TimePoint target, uint64_t pos, uint64_t generation) = 0;

    // Streams that stop themselves at gateOpen() only need waking on open; others override this.
    virtual void applyFlowGate(bool closed);

    // Also closed while the loader is behind, since cacheEnd counts parsed data only.
    [[nodiscard]] bool gateOpen() const noexcept {
        return !stopped_.load(std::memory_order_acquire) && !gate_.isPaused() &&
               pendingChunks_.load(std::memory_order_acquire) < kMaxPendingChunks;
    }

    // Waking per chunk would undo a watermark pause for streams driven by applyFlowGate.
    [[nodiscard]] virtual bool pullsGate() const {
        return true;
    }

 public:
    // Exposed so tests can pin each source's choice; nothing outside the source needs it.
    [[nodiscard]] bool pullsGateForTest() const {
        return pullsGate();
    }

 protected:
    // Whether the stream is positioned by time (HLS) or by the byte offset the demuxer returns.
    [[nodiscard]] virtual bool seeksByTime() const {
        return false;
    }

    // Every stream callback is marshalled onto it; valid from the first createStream() call.
    [[nodiscard]] const std::shared_ptr<TaskThread>& loader() const noexcept {
        return loader_;
    }

    // The result is written back into the frame and feeds cacheEnd.
    [[nodiscard]] virtual double mapPts(const AVFrame& frame) const noexcept {
        return frame.ptsTime() - rangeStartSec();
    }

    // Playback time at which the source raises EOS on its own; negative means unlimited.
    [[nodiscard]] virtual double playbackEndSec() const noexcept {
        return options_.playbackRange.valid()
                   ? options_.playbackRange.endSec - options_.playbackRange.startSec
                   : -1.0;
    }

    [[nodiscard]] double rangeStartSec() const noexcept {
        return options_.playbackRange.valid() ? options_.playbackRange.startSec : 0.0;
    }

    // Runs on the loader for every packet that passes the generation check, before the demuxer.
    virtual void onPacket(const DataPacket& packet) {
        (void)packet;
    }

    // Updates the live/seekable/duration facts and wakes the engine when they changed.
    void mergeStreamFacts(bool isLive, bool isSeekable, double durationSec);

    std::shared_ptr<IStream> stream_;
    const SourceOptions options_;

 private:
    void onChunk(DataChunk chunk);
    void onStreamComplete(uint64_t generation);
    void onStreamError(Error error, int32_t origin);
    void openOnLoader();
    void seekOnLoader(Time::TimePoint target, SeekEpoch epoch);
    void refreshTracksOnLoader();
    void feedDemuxer(DataPacket& packet);
    void refreshStreamFacts();
    bool createDemuxer();
    void openDemuxer();
    void publishFrames(DemuxerResult& result);
    void postEos();
    void updateGate();
    void postFlowGate(bool closed);
    void fail(Error error, int32_t origin = 0);
    bool repositionStream(uint64_t pos);
    void resetCache(Time::TimePoint to);

    struct TrackSnapshot {
        std::string container;
        std::string videoCodec;
        int videoProfile = 0;
        int videoLevel = 0;
        uint32_t videoWidth = 0;
        uint32_t videoHeight = 0;
        uint32_t videoFps = 0;
        double frameDurationMs = 0.0;
        int32_t rotationDegrees = 0;
        uint32_t videoTimeScale = 0;
        std::string parameterSets;
        std::string audioCodec;
        std::string audioProfile;
        uint64_t audioSampleRate = 0;
        uint16_t audioChannels = 0;
        uint16_t audioBitsPerSample = 0;
        uint64_t audioBitrate = 0;
        uint32_t audioTimeScale = 0;
    };

    // Written on the loader thread, read by debugInfo() from any thread.
    mutable std::mutex trackMutex_;
    TrackSnapshot track_;

    std::shared_ptr<TaskThread> loader_;
    std::shared_ptr<IDemuxer> demuxer_;
    std::unique_ptr<Buffer> probe_;
    bool opened_ = false;
    bool hasAudio_ = false;
    bool hasVideo_ = false;
    bool needKeyFrame_ = true;
    uint64_t pendingOffset_ = 0;
    bool verifyOffset_ = false;
    // epoch_ tags frames for the engine; the stream has its own counter that only changes on seek.
    SeekEpoch epoch_;
    uint64_t streamGeneration_ = 0;

    SenderPtr<AVFrameRefPtr> audioTx_;
    SenderPtr<AVFrameRefPtr> videoTx_;
    ReceiverPtr<AVFrameRefPtr> audioRx_;
    ReceiverPtr<AVFrameRefPtr> videoRx_;

    WatermarkGate<double> gate_;
    const Wake wake_;

    mutable std::mutex infoMutex_;
    MediaInfo info_;
    std::optional<Error> failure_;
    int32_t failureOrigin_ = 0;
    std::atomic<uint64_t> audioEndUs_{0};
    std::atomic<uint64_t> videoEndUs_{0};
    static constexpr uint32_t kMaxPendingChunks = 2;

    // requestedEpoch_ is written by callers and appliedEpoch_ by the loader; equal means caught up.
    std::atomic<SeekEpoch> requestedEpoch_;
    std::atomic<SeekEpoch> appliedEpoch_;
    std::atomic<uint64_t> targetUs_{0};
    std::atomic<uint32_t> pendingChunks_{0};
    std::atomic<uint64_t> clockUs_{0};
    std::atomic<bool> eos_{false};
    std::atomic<bool> closed_{false};
    std::atomic<bool> stopped_{false};
};

}  // namespace slark::engine
