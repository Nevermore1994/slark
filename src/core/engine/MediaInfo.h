//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "Time.hpp"

namespace slark::engine {

// isLive and isSeekable may change after open: HLS learns them from the playlist.
struct MediaInfo {
    bool tracksKnown = false;
    bool hasAudio = false;
    bool hasVideo = false;
    bool isLive = false;
    bool isSeekable = false;
    Time::TimePoint duration{};

    bool operator==(const MediaInfo&) const = default;
};

}  // namespace slark::engine
