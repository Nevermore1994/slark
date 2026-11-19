//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <optional>
#include <string>

#include "DebugInfo.h"
#include "Error.h"
#include "SeekEpoch.h"
#include "SeekMode.h"
#include "Time.hpp"

namespace slark::engine {

class MediaClock;

class Pipeline {
 public:
    virtual ~Pipeline() = default;

    virtual void flush(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) = 0;

    // Same epoch, new target: keep the data, only change what counts as "before the target".
    virtual void retarget(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) = 0;
    virtual void setClock(MediaClock& clock) = 0;

    // Data keeps flowing while paused so that starting is instant.
    virtual void setPlaying(bool playing) {
        (void)playing;
    }

    virtual void setVolume(float volume) {
        (void)volume;
    }

    // True once a video frame has reached the screen in this session.
    [[nodiscard]] virtual bool presentedFrame() const {
        return false;
    }

    // vsync tick from the render side; only the video pipeline uses it for A/V sync.
    virtual void onVsync(Time::TimePoint clockNow) {
        (void)clockNow;
    }

    [[nodiscard]] virtual bool ready(SeekEpoch epoch, Time::TimePoint target) const = 0;
    [[nodiscard]] virtual bool ended() const = 0;
    [[nodiscard]] virtual std::optional<Error> failure() const = 0;

    [[nodiscard]] virtual bool blocksReady() const {
        return true;
    }

    // One line of gating state for diagnostics (why this pipeline is or is not ready yet).
    [[nodiscard]] virtual std::string debugState() const {
        return {};
    }

    // Diagnostics only: implementations copy values under their own locks.
    [[nodiscard]] virtual DebugInfo debugInfo() const {
        return {};
    }

    // Wall deadline on the scheduler timeline.
    [[nodiscard]] virtual std::optional<Time::TimePoint> nextWakeup() const {
        return std::nullopt;
    }
};

}  // namespace slark::engine
