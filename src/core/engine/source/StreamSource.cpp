//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "StreamSource.h"

#include <algorithm>
#include <format>
#include <utility>

#include "DebugLines.h"
#include "DemuxerManager.h"
#include "Log.hpp"
#include "ScopeGuard.h"
#include "StringUtil.h"
#include "TaskThread.h"

namespace slark::engine {
namespace {

uint64_t toUs(double seconds) noexcept {
    return seconds <= 0.0 ? 0 : static_cast<uint64_t>(seconds * Time::kMicroSecondScale);
}

void raiseTo(std::atomic<uint64_t>& end, uint64_t value) noexcept {
    auto prev = end.load(std::memory_order_acquire);
    while (value > prev && !end.compare_exchange_weak(prev, value, std::memory_order_acq_rel)) {
    }
}

AVFrameRefPtr makeEos(AVFrameType type, SeekEpoch epoch) {
    auto eos = std::make_shared<AVFrame>(type);
    eos->isEOS = true;
    eos->seekEpoch = epoch;
    if (type == AVFrameType::Audio) {
        auto info = std::make_shared<AudioFrameInfo>();
        info->isEndOfStream = true;
        eos->info = std::move(info);
    } else {
        auto info = std::make_shared<VideoFrameInfo>();
        info->isEndOfStream = true;
        eos->info = std::move(info);
    }
    return eos;
}

}  // namespace

StreamSource::StreamSource(SourceOptions options, SeekEpoch epoch, Wake wake)
    : options_(std::move(options)),
      epoch_(epoch),
      gate_(WatermarkConfig<double>{options_.flow.source.lowWatermarkSec,
                                    options_.flow.source.highWatermarkSec},
            {.closeGate =
                 [this] {
                     postFlowGate(true);
                 },
             .openGate =
                 [this] {
                     postFlowGate(false);
                 }}),
      wake_(std::move(wake)),
      requestedEpoch_(epoch),
      appliedEpoch_(epoch) {
    std::tie(audioTx_, audioRx_) = Channel<AVFrameRefPtr>::create();
    std::tie(videoTx_, videoRx_) = Channel<AVFrameRefPtr>::create();
}

StreamSource::~StreamSource() {
    close();
}

void StreamSource::start() {
    loader_ = TaskThread::create("sourceLoader");
    if (!loader_) {
        fail(Error::OutOfMemory);
        return;
    }
    (void)loader_->postTask(ThreadTask([this] {
        if (!closed_.load(std::memory_order_acquire)) {
            openOnLoader();
        }
    }));
}

void StreamSource::close() {
    if (closed_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (loader_) {
        loader_->stop();
        loader_->join();
    }
    // The loader has exited, so stream_ is no longer shared with any other thread.
    if (stream_) {
        stream_->close();
    }
    stream_.reset();
}

MediaInfo StreamSource::info() const {
    std::lock_guard lock(infoMutex_);
    return info_;
}

std::shared_ptr<AudioInfo> StreamSource::audioTrackInfo() const {
    std::lock_guard lock(infoMutex_);
    return demuxer_ ? demuxer_->audioInfo() : nullptr;
}

std::shared_ptr<VideoInfo> StreamSource::videoTrackInfo() const {
    std::lock_guard lock(infoMutex_);
    return demuxer_ ? demuxer_->videoInfo() : nullptr;
}

std::optional<Error> StreamSource::failure() const {
    std::lock_guard lock(infoMutex_);
    return failure_;
}

ErrorDetail StreamSource::failureDetail() const {
    std::lock_guard lock(infoMutex_);
    return ErrorDetail{failureOrigin_, StringUtil::extractHostFromUri(options_.uri)};
}

Time::TimePoint StreamSource::cacheEnd() const {
    return Time::TimePoint(std::max(audioEndUs_.load(std::memory_order_acquire),
                                    videoEndUs_.load(std::memory_order_acquire)));
}

bool StreamSource::isLoading() const {
    return !eos_.load(std::memory_order_acquire);
}

Receiver<AVFrameRefPtr>* StreamSource::frames(TrackType type) {
    std::lock_guard lock(infoMutex_);
    if (!info_.tracksKnown) {
        return nullptr;
    }
    if (type == TrackType::Audio) {
        return hasAudio_ ? audioRx_.get() : nullptr;
    }
    return type == TrackType::Video && hasVideo_ ? videoRx_.get() : nullptr;
}

DebugInfo StreamSource::debugInfo() const {
    TrackSnapshot track;
    {
        std::lock_guard lock(trackMutex_);
        track = track_;
    }
    DebugInfo result;
    DebugLines source;
    DebugLines video;
    DebugLines audio;
    DebugLines buffer;

    const auto mediaInfo = info();
    const auto streamInfo = stream_ ? stream_->info() : StreamInfo{};
    source.add("URL", options_.uri);
    source.add("Protocol", [&] {
        switch (streamInfo.type) {
            case StreamType::Http:
                return "HTTP/HTTPS";
            case StreamType::HLS:
                return "HLS";
            case StreamType::File:
                return "File";
            default:
                return "Unknown";
        }
    }());
    source.add("Container", track.container);
    source.add("File size", streamInfo.totalSize, "bytes");
    source.add("Duration", mediaInfo.duration.second(), "s");
    source.add("Live", mediaInfo.isLive);
    source.add("Seekable", mediaInfo.isSeekable);
    source.add("Tracks known", mediaInfo.tracksKnown);
    source.add("Track count", static_cast<int>(mediaInfo.hasAudio) +
                                  static_cast<int>(mediaInfo.hasVideo));

    if (!track.videoCodec.empty()) {
        video.add("Codec", track.videoCodec);
        video.add("Profile", track.videoProfile);
        video.add("Level", track.videoLevel);
        video.add("Resolution", std::format("{}x{}", track.videoWidth, track.videoHeight));
        video.add("FPS", track.videoFps);
        video.add("Frame duration", track.frameDurationMs, "ms");
        video.add("Rotation", track.rotationDegrees, "deg");
        video.add("Time scale", track.videoTimeScale);
        video.add("Parameter sets", track.parameterSets);
        video.add("Bitrate", "n/a");
    }
    if (!track.audioCodec.empty()) {
        audio.add("Codec", track.audioCodec);
        audio.add("Profile", track.audioProfile);
        audio.add("Sample rate", track.audioSampleRate, "Hz");
        audio.add("Channels", track.audioChannels);
        audio.add("Bits per sample", track.audioBitsPerSample);
        audio.add("Bitrate", track.audioBitrate, "bit/s");
        audio.add("Time scale", track.audioTimeScale);
    }

    buffer.add("Cache end", cacheEnd().second(), "s");
    buffer.add("Audio end", Time::TimePoint(audioEndUs_.load()).second(), "s");
    buffer.add("Video end", Time::TimePoint(videoEndUs_.load()).second(), "s");
    buffer.add("Low watermark", options_.flow.source.lowWatermarkSec, "s");
    buffer.add("High watermark", options_.flow.source.highWatermarkSec, "s");
    buffer.add("Gate closed", gate_.isClosed());
    buffer.add("Gate forced", gate_.isForced());
    buffer.add("Gate held", gate_.isHeld());
    buffer.add("Pending chunks", pendingChunks_.load(std::memory_order_relaxed));
    buffer.add("Max pending chunks", kMaxPendingChunks);
    buffer.add("Loading", isLoading());

    result.source = source.take();
    result.video = video.take();
    result.audio = audio.take();
    result.buffer = buffer.take();
    if (stream_) {
        result.append(stream_->debugInfo());
    }
    return result;
}

void StreamSource::applyFlowGate(bool closed) {
    if (!closed && stream_) {
        stream_->openFetchGate();
    }
}

void StreamSource::postFlowGate(bool closed) {
    if (!loader_ || closed_.load(std::memory_order_acquire)) {
        return;
    }
    (void)loader_->postTask(ThreadTask([this, closed] {
        if (stream_ && !closed_.load(std::memory_order_acquire)) {
            applyFlowGate(closed);
        }
    }));
}

void StreamSource::setSeeking(bool seeking) noexcept {
    if (closed_.load(std::memory_order_acquire) || !loader_) {
        return;
    }
    (void)loader_->postTask(ThreadTask([this, seeking] {
        if (closed_.load(std::memory_order_acquire)) {
            return;
        }
        if (seeking) {
            gate_.holdOpen();
        } else {
            gate_.releaseHold();
            updateGate();
        }
    }));
}

void StreamSource::regulate(Time::TimePoint clockNow) {
    if (closed_.load(std::memory_order_acquire)) {
        return;
    }
    clockUs_.store(clockNow.point(), std::memory_order_release);
    if (!loader_) {
        updateGate();
        return;
    }
    // Must run on the loader: two threads deciding from stale levels made the gate flap.
    (void)loader_->postTask(ThreadTask([this] {
        updateGate();
    }));
}

// Also run after every publish so the gate reacts within one chunk, not on the next reconcile.
void StreamSource::updateGate() {
    if (closed_.load(std::memory_order_acquire)) {
        return;
    }
    const Time::TimePoint clock(clockUs_.load(std::memory_order_acquire));
    gate_.update((cacheEnd() - clock).second());
}

bool StreamSource::retarget(Time::TimePoint target) {
    if (appliedEpoch_.load(std::memory_order_acquire) !=
            requestedEpoch_.load(std::memory_order_acquire) ||
        target.point() < targetUs_.load(std::memory_order_acquire) || target > cacheEnd()) {
        return false;
    }
    targetUs_.store(target.point(), std::memory_order_release);
    return true;
}

void StreamSource::seekTo(Time::TimePoint target, SeekEpoch epoch) {
    if (!loader_) {
        return;
    }
    targetUs_.store(target.point(), std::memory_order_release);
    requestedEpoch_.store(epoch, std::memory_order_release);
    (void)loader_->postTask(ThreadTask([this, target, epoch] {
        seekOnLoader(target, epoch);
    }));
}

void StreamSource::fail(Error error, int32_t origin) {
    {
        std::lock_guard lock(infoMutex_);
        if (!failure_) {
            failure_ = error;
            failureOrigin_ = origin;
        }
    }
    if (wake_) {
        wake_();
    }
}

void StreamSource::openOnLoader() {
    auto created = createStream();
    if (!created) {
        fail(created.error());
        return;
    }
    stream_ = std::move(*created);

    OpenParams params;
    params.uri = options_.uri;
    params.range = options_.range;
    params.onData = [this](DataChunk chunk) {
        onChunk(std::move(chunk));
    };
    params.onComplete = [this](uint64_t generation) {
        onStreamComplete(generation);
    };
    params.onError = [this](Error error, int32_t origin) {
        onStreamError(error, origin);
    };
    stream_->setProductionGate([this] {
        return gateOpen();
    });

    auto result = stream_->open(params);
    if (!result) {
        fail(result.error());
        return;
    }
    {
        std::lock_guard lock(infoMutex_);
        info_.isSeekable = result->isSeekable;
        info_.isLive = result->isLive;
    }
    stream_->start();
}

void StreamSource::onChunk(DataChunk chunk) {
    if (closed_.load(std::memory_order_acquire) || chunk.empty()) {
        return;
    }
    DataPacket packet;
    packet.data = std::make_unique<Data>(chunk.data);
    packet.offset = chunk.offset;
    packet.segmentIndex = chunk.segmentIndex;
    packet.isLastChunk = chunk.isLastChunk;
    packet.sourceGeneration = chunk.sourceGeneration;
    auto holder = std::make_shared<DataPacket>(std::move(packet));
    pendingChunks_.fetch_add(1, std::memory_order_acq_rel);
    const bool posted = loader_
                            ->postTask(ThreadTask([this, holder] {
                                if (!closed_.load(std::memory_order_acquire)) {
                                    feedDemuxer(*holder);
                                }
                                pendingChunks_.fetch_sub(1, std::memory_order_acq_rel);
                                if (stream_ && pullsGate()) {
                                    stream_->openFetchGate();
                                }
                            }))
                            .has_value();
    if (!posted) {
        pendingChunks_.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void StreamSource::onStreamComplete(uint64_t generation) {
    (void)loader_->postTask(ThreadTask([this, generation] {
        if (generation == streamGeneration_ && !closed_.load(std::memory_order_acquire)) {
            postEos();
        }
    }));
}

void StreamSource::onStreamError(Error error, int32_t origin) {
    fail(error, origin);
}

void StreamSource::refreshStreamFacts() {
    if (!stream_) {
        return;
    }
    const auto info = stream_->info();
    // Duration comes from the demuxer for plain streams, so only the flags are synced here.
    mergeStreamFacts(info.isLive, info.isSeekable, 0.0);
}

void StreamSource::feedDemuxer(DataPacket& packet) {
    if (packet.sourceGeneration != streamGeneration_) {
        return;
    }
    refreshStreamFacts();
    onPacket(packet);
    if (verifyOffset_) {
        if (packet.offset != pendingOffset_) {
            return;
        }
        verifyOffset_ = false;
    }

    if (!opened_) {
        if (!probe_) {
            probe_ = std::make_unique<Buffer>();
        }
        if (!probe_->append(packet.offset, std::move(packet.data))) {
            return;
        }
        if (!demuxer_ && !createDemuxer()) {
            return;
        }
        openDemuxer();
        return;
    }

    auto result = demuxer_->parseData(packet);
    LogIThrottle(500, "parse: a={} v={} epoch={} needKey={} pending={}", result.audioFrames.size(),
                 result.videoFrames.size(), epoch_, needKeyFrame_, pendingOffset_);
    refreshTracksOnLoader();
    publishFrames(result);
    if (result.resultCode == DemuxerResultCode::FileEnd) {
        postEos();
    }
}

// Until tracks appear (TS announces them in-stream) frames() stays null and EOS is withheld.
void StreamSource::refreshTracksOnLoader() {
    if (!demuxer_ || (!demuxer_->hasAudio() && !demuxer_->hasVideo())) {
        return;
    }

    TrackSnapshot snapshot;
    switch (demuxer_->type()) {
        case DemuxerType::MP4:
            snapshot.container = "MP4/fMP4";
            break;
        case DemuxerType::TS:
            snapshot.container = "TS";
            break;
        case DemuxerType::WAV:
            snapshot.container = "WAV";
            break;
        case DemuxerType::HLS:
            snapshot.container = "HLS";
            break;
        default:
            snapshot.container = "Unknown";
            break;
    }
    if (const auto info = demuxer_->videoInfo()) {
        snapshot.videoCodec = info->mediaInfo;
        snapshot.videoProfile = info->profile;
        snapshot.videoLevel = info->level;
        snapshot.videoWidth = info->width;
        snapshot.videoHeight = info->height;
        snapshot.videoFps = info->fps;
        snapshot.frameDurationMs = static_cast<double>(info->frameDurationMs().count());
        snapshot.rotationDegrees = info->rotationDegrees;
        snapshot.videoTimeScale = info->timeScale;
        snapshot.parameterSets = std::format("sps={} pps={} vps={}", info->sps != nullptr,
                                             info->pps != nullptr, info->vps != nullptr);
    }
    if (const auto info = demuxer_->audioInfo()) {
        snapshot.audioCodec = info->mediaInfo;
        snapshot.audioProfile = std::to_string(static_cast<int>(info->profile));
        snapshot.audioSampleRate = info->sampleRate;
        snapshot.audioChannels = info->channels;
        snapshot.audioBitsPerSample = info->bitsPerSample;
        snapshot.audioBitrate = info->bitrate();
        snapshot.audioTimeScale = info->timeScale;
    }
    {
        std::lock_guard lock(trackMutex_);
        track_ = std::move(snapshot);
    }

    bool becameKnown = false;
    {
        std::lock_guard lock(infoMutex_);
        if (!info_.tracksKnown) {
            hasAudio_ = demuxer_->hasAudio();
            hasVideo_ = demuxer_->hasVideo();
            info_.tracksKnown = true;
            info_.hasAudio = hasAudio_;
            info_.hasVideo = hasVideo_;
            auto seconds = demuxer_->totalDuration().second();
            if (seconds > 0) {
                if (options_.playbackRange.valid()) {
                    seconds = std::max(
                        0.0, std::min(seconds, options_.playbackRange.endSec) - rangeStartSec());
                }
                info_.duration = Time::TimePoint::fromSeconds(seconds);
            }
            becameKnown = true;
        }
    }
    if (becameKnown && wake_) {
        wake_();
    }
}

bool StreamSource::createDemuxer() {
    if (options_.demuxerFactory) {
        demuxer_ = options_.demuxerFactory();
        if (!demuxer_) {
            fail(Error::PipelineFormatNotSupported);
            return false;
        }
        demuxer_->init(DemuxerConfig{options_.uri});
        return true;
    }
    const auto type = options_.demuxer != DemuxerType::Unknown
                          ? options_.demuxer
                          : DemuxerManager::shareInstance().probeDemuxType(probe_->view());
    demuxer_ = DemuxerManager::shareInstance().create(type);
    if (!demuxer_) {
        fail(Error::PipelineFormatNotSupported);
        return false;
    }
    demuxer_->init(DemuxerConfig{options_.uri});
    return true;
}

void StreamSource::openDemuxer() {
    if (!demuxer_->open(probe_)) {
        if (const auto seek = demuxer_->pendingSeekPos()) {
            demuxer_->resetPendingSeek();
            probe_ = std::make_unique<Buffer>();
            repositionStream(*seek);
        }
        return;
    }
    opened_ = true;
    refreshTracksOnLoader();
    probe_.reset();
    // A seek requested while opening was dropped by seekOnLoader(); apply it now.
    if (requestedEpoch_.load(std::memory_order_acquire) !=
        appliedEpoch_.load(std::memory_order_acquire)) {
        seekOnLoader(Time::TimePoint(targetUs_.load(std::memory_order_acquire)),
                     requestedEpoch_.load(std::memory_order_acquire));
        if (wake_) {
            wake_();
        }
        return;
    }
    if (const auto header = demuxer_->headerInfo(); header && header->headerLength > 0) {
        demuxer_->seekPos(header->headerLength);
        repositionStream(header->headerLength);
    }
    if (wake_) {
        wake_();
    }
}

void StreamSource::publishFrames(DemuxerResult& result) {
    if (eos_.load(std::memory_order_acquire)) {
        // EOS was raised by an earlier batch; nothing more may be published.
        return;
    }
    bool posted = false;
    const auto end = playbackEndSec();
    auto publish = [&](AVFramePtr& frame, std::atomic<uint64_t>& trackEnd) {
        auto pts = mapPts(*frame);
        if (end >= 0 && pts >= end) {
            postEos();
            return false;
        }
        raiseTo(trackEnd, toUs(pts));
        frame->pts =
            static_cast<int64_t>(std::llround(pts * static_cast<double>(frame->timeScale)));
        frame->seekEpoch = epoch_;
        return true;
    };
    for (auto& frame : result.audioFrames) {
        if (publish(frame, audioEndUs_)) {
            audioTx_->post(AVFrameRefPtr(std::move(frame)));
            posted = true;
        }
    }
    for (auto& frame : result.videoFrames) {
        if (needKeyFrame_) {
            if (!frame->info || !frame->info->isKeyFrame()) {
                LogIThrottle(500, "video drop: waiting for a keyframe, pts={:.3f}",
                             frame->ptsTime());
                continue;
            }
            needKeyFrame_ = false;
            LogI("video keyframe after seek: pts={:.3f}", frame->ptsTime());
        }
        if (publish(frame, videoEndUs_)) {
            videoTx_->post(AVFrameRefPtr(std::move(frame)));
            posted = true;
        }
    }
    if (posted) {
        updateGate();
        if (wake_) {
            wake_();
        }
    }
}

void StreamSource::mergeStreamFacts(bool isLive, bool isSeekable, double durationSec) {
    bool changed = false;
    {
        std::lock_guard lock(infoMutex_);
        changed |= std::exchange(info_.isLive, isLive) != isLive;
        changed |= std::exchange(info_.isSeekable, isSeekable) != isSeekable;
        if (durationSec > 0) {
            const auto duration = Time::TimePoint::fromSeconds(durationSec);
            changed |= info_.duration != duration;
            info_.duration = duration;
        }
    }
    if (changed && wake_) {
        wake_();
    }
}

void StreamSource::postEos() {
    if (eos_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    {
        std::lock_guard lock(infoMutex_);
        if (hasAudio_) {
            audioTx_->post(makeEos(AVFrameType::Audio, epoch_));
        }
        if (hasVideo_) {
            videoTx_->post(makeEos(AVFrameType::Video, epoch_));
        }
    }
    if (wake_) {
        wake_();
    }
}

bool StreamSource::repositionStream(uint64_t pos) {
    const auto next = streamGeneration_ + 1;
    pendingOffset_ = pos;
    verifyOffset_ = true;
    if (auto result = stream_->seek(pos, next); !result) {
        verifyOffset_ = false;
        fail(result.error());
        return false;
    }
    streamGeneration_ = next;
    return true;
}

void StreamSource::resetCache(Time::TimePoint to) {
    audioEndUs_.store(to.point(), std::memory_order_release);
    videoEndUs_.store(to.point(), std::memory_order_release);
    eos_.store(false, std::memory_order_release);
}

void StreamSource::seekOnLoader(Time::TimePoint target, SeekEpoch epoch) {
    // A newer seek is already queued behind this one: skip, it will do the work.
    if (closed_.load(std::memory_order_acquire) ||
        epoch != requestedEpoch_.load(std::memory_order_acquire)) {
        return;
    }
    if (!opened_) {
        // The open path performs this seek once ready; dropping it leaves the engine in Seeking.
        return;
    }
    const ScopeGuard applied([this, epoch] {
        appliedEpoch_.store(epoch, std::memory_order_release);
    });
    epoch_ = epoch;
    needKeyFrame_ = true;
    verifyOffset_ = false;
    resetCache(target);
    // The gate may still be closed from the pre-seek cache; nothing else would reopen it.
    gate_.reset();
    (void)audioRx_->tryReceiveAll();
    (void)videoRx_->tryReceiveAll();
    demuxer_->flush();

    const auto mediaTarget = target.second() + rangeStartSec();
    const auto pos = seeksByTime() ? 0 : demuxer_->getSeekToPos(mediaTarget);
    demuxer_->seekPos(pos);
    const auto next = streamGeneration_ + 1;
    if (auto result = seekStream(target, pos, next); !result) {
        fail(result.error());
        return;
    }
    streamGeneration_ = next;
    pendingOffset_ = pos;
    verifyOffset_ = !seeksByTime();
}

}  // namespace slark::engine
