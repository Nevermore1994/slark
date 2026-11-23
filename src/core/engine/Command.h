//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <string>
#include <variant>

#include "Error.h"
#include "ErrorDetail.h"
#include "PlaybackRange.h"
#include "PlayerTypes.h"
#include "RepeatMode.h"
#include "SeekEpoch.h"
#include "SeekMode.h"
#include "Status.h"
#include "Time.hpp"

namespace slark::engine {

struct Prepare {
    ResourceItem item;
    PlayerSetting setting;
    Time::TimePoint startPos{};
    // Mapped from ResourceItem::displayStart/displayDuration by the player.
    PlaybackRange playbackRange;
    // Routes HTTP request metrics to the owning perf session.
    std::string perfSessionId;
};

struct Play {};

struct Pause {};

struct Seek {
    Time::TimePoint target;
    SeekMode mode = SeekMode::Exact;
};

struct SetSpeed {
    double speed = 1.0;
};

struct SetRepeat {
    RepeatMode mode = RepeatMode::Off;
};

struct SetVolume {
    float volume = 1.0f;
};

struct SetMute {
    bool muted = false;
};

struct Stop {};

struct Release {};

using Command = std::
    variant<Prepare, Play, Pause, Seek, SetSpeed, SetRepeat, SetVolume, SetMute, Stop, Release>;

struct SeekDone {};

struct ErrorEvent {
    Error code = Error::Success;
    bool fatal = false;
    ErrorDetail detail;
};

struct PlayEnd {};

struct FirstFrameRendered {};

using Notification = std::variant<SeekDone, ErrorEvent, PlayEnd, FirstFrameRendered>;

struct Snapshot {
    Status status = Status::Idle;
    bool wantPlay = false;
    bool isPlaying = false;
    bool isLoading = false;
    bool muted = false;
    bool hasAudio = false;
    bool hasVideo = false;
    bool isSeekable = false;
    bool tracksKnown = false;
    Time::TimePoint position{};
    Time::TimePoint buffered{};
    Time::TimePoint duration{};
    double speed = 1.0;
    float volume = 1.0f;
};

}  // namespace slark::engine
