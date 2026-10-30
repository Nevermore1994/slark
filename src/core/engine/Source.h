//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <memory>
#include <optional>

#include "AVFrame.hpp"
#include "AudioInfo.h"
#include "Channel.hpp"
#include "Error.h"
#include "ErrorDetail.h"
#include "MediaInfo.h"
#include "SeekEpoch.h"
#include "Time.hpp"
#include "VideoInfo.h"

namespace slark::engine {

class Source {
 public:
    virtual ~Source() = default;

    [[nodiscard]] virtual MediaInfo info() const = 0;
    [[nodiscard]] virtual Time::TimePoint cacheEnd() const = 0;
    [[nodiscard]] virtual bool isLoading() const = 0;
    [[nodiscard]] virtual std::optional<Error> failure() const = 0;
    [[nodiscard]] virtual ErrorDetail failureDetail() const {
        return {};
    }

    // null until the tracks are known, and when the track does not exist.
    [[nodiscard]] virtual std::shared_ptr<AudioInfo> audioTrackInfo() const {
        return nullptr;
    }

    [[nodiscard]] virtual std::shared_ptr<VideoInfo> videoTrackInfo() const {
        return nullptr;
    }

    // null when the media has no such track; the consumer drops frames from stale seek epochs.
    [[nodiscard]] virtual Receiver<AVFrameRefPtr>* frames(TrackType type) = 0;

    virtual void seekTo(Time::TimePoint target, SeekEpoch epoch) = 0;

    // Returns false when a real seekTo is needed.
    virtual bool retarget(Time::TimePoint target) = 0;
    virtual void regulate(Time::TimePoint clockNow) = 0;

    // Keep loading while a seek is in flight: the frozen clock makes the cache look full.
    virtual void setSeeking(bool seeking) {
        (void)seeking;
    }

    virtual void close() = 0;
};

}  // namespace slark::engine
