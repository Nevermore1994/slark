//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <optional>

#include "Error.h"
#include "MediaInfo.h"
#include "RepeatMode.h"
#include "SeekEpoch.h"
#include "SeekMode.h"
#include "Status.h"
#include "Time.hpp"

namespace slark::engine {

// Engine thread only. Facts are authoritative; Status is a derived result cached in Engine.
struct EngineState {
    bool wantPlay = false;
    std::optional<Time::TimePoint> seekTarget;
    SeekMode seekMode = SeekMode::Exact;
    SeekEpoch issuedEpoch = kInvalidSeekEpoch;
    double speed = 1.0;
    RepeatMode repeat = RepeatMode::Off;
    float volume = 1.0f;
    bool muted = false;

    bool hasSession = false;
    MediaInfo info;
    std::optional<Error> failure;
};

struct PipelineView {
    bool blockingNotReady = false;
    // Must be false without pipelines, or a session with known tracks reads as Ended.
    bool allEnded = false;
    bool aheadBelowStart = false;
};

[[nodiscard]] constexpr Status deriveStatus(const EngineState& state,
                                            const PipelineView& pipelines) noexcept {
    if (state.failure) {
        return Status::Failed;
    }
    if (!state.hasSession) {
        return Status::Idle;
    }
    if (!state.info.tracksKnown) {
        return Status::Opening;
    }
    if (state.seekTarget) {
        return Status::Seeking;
    }
    if (pipelines.blockingNotReady || pipelines.aheadBelowStart) {
        return Status::Buffering;
    }
    if (pipelines.allEnded) {
        return Status::Ended;
    }
    return Status::Ready;
}

[[nodiscard]] constexpr bool isPlaying(const EngineState& state, Status status) noexcept {
    return state.wantPlay && status == Status::Ready;
}

}  // namespace slark::engine
