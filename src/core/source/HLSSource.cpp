//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HLSSource.h"

#include <cmath>
#include <utility>

#include "StreamFactory.hpp"

namespace slark::engine {
namespace {

// A raw PTS drop larger than this is re-anchored even without an EXT-X-DISCONTINUITY marker.
constexpr double kPtsResetThresholdSec = 0.5;

}  // namespace

std::unique_ptr<HLSSource> HLSSource::create(SourceOptions options, SeekEpoch epoch, Wake wake) {
    std::unique_ptr<HLSSource> source(new HLSSource(std::move(options), epoch, std::move(wake)));
    source->anchorSec_ = source->rangeStartSec();
    source->start();
    return source;
}

StreamResult<std::shared_ptr<IStream>> HLSSource::createStream() {
    auto stream = StreamFactory::createHLSStream(options_.uri, loader());
    if (!stream) {
        return std::unexpected(Error::SchemeNotSupported);
    }
    hls_ = std::static_pointer_cast<slark::HLSStream>(*stream);
    return std::move(*stream);
}

StreamResult<void> HLSSource::seekStream(Time::TimePoint target, uint64_t, uint64_t generation) {
    const auto mediaTarget = target.second() + rangeStartSec();
    const auto segment = hls_->segmentInfoAtTime(mediaTarget);
    if (auto result = stream_->seekByTime(mediaTarget, generation); !result) {
        return result;
    }
    if (segment) {
        anchorSec_ = segment->startTime;
    }
    if (segment && segment->isDiscontinuity) {
        audioTimeline_ = {};
        videoTimeline_ = {};
    } else {
        audioTimeline_.lastPlaybackSec.reset();
        videoTimeline_.lastPlaybackSec.reset();
        audioTimeline_.discontinuityPending = false;
        videoTimeline_.discontinuityPending = false;
    }
    hasCurrentSequence_ = false;
    return {};
}

double HLSSource::mapPts(const AVFrame& frame) const noexcept {
    auto& timeline = frame.frameType == AVFrameType::Video ? videoTimeline_ : audioTimeline_;
    const auto raw = frame.ptsTime();
    if (!timeline.anchorRawPts) {
        timeline.anchorRawPts = raw;
        timeline.offsetSec = anchorSec_ - raw;
    }
    auto pts = raw + timeline.offsetSec;
    const bool discontinuity = std::exchange(timeline.discontinuityPending, false);
    if (timeline.lastPlaybackSec &&
        (discontinuity || pts < *timeline.lastPlaybackSec - kPtsResetThresholdSec)) {
        timeline.offsetSec += *timeline.lastPlaybackSec - pts;
        pts = *timeline.lastPlaybackSec;
    }
    timeline.lastPlaybackSec = pts;
    return pts - rangeStartSec();
}

double HLSSource::playbackEndSec() const noexcept {
    return options_.playbackRange.valid() ? options_.playbackRange.endSec
                                                - options_.playbackRange.startSec
                                          : -1.0;
}

void HLSSource::onPacket(const DataPacket& packet) {
    if (!hasCurrentSequence_ || packet.segmentIndex != currentSequence_) {
        currentSequence_ = packet.segmentIndex;
        hasCurrentSequence_ = true;
        const bool discontinuity =
            hls_->segmentInfo(packet.segmentIndex).value_or(slark::Segment{}).isDiscontinuity;
        audioTimeline_.discontinuityPending = discontinuity;
        videoTimeline_.discontinuityPending = discontinuity;
    }
    const auto info = stream_->info();
    auto duration = info.duration;
    if (options_.playbackRange.valid()) {
        duration = std::min(duration, options_.playbackRange.endSec) - rangeStartSec();
    }
    mergeStreamFacts(info.isLive, info.isSeekable, duration);
}

}  // namespace slark::engine
