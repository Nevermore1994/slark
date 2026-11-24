//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AudioInfo.h"
#include "IDecoder.h"
#include "IEGLContext.h"
#include "PlayerTypes.h"
#include "Time.hpp"
#include "pipeline/AudioSink.h"
#include "pipeline/VideoSink.h"

namespace slark::engine {

struct Prepare;
class Source;
class Pipeline;

// Samples are rebased so the window starts at 0; invalid means the whole media.
struct PlaybackRange {
    double startSec = 0.0;
    double endSec = 0.0;

    [[nodiscard]] bool valid() const noexcept {
        return endSec > startSec;
    }
};

enum class Status : uint8_t {
    Idle,
    Opening,
    Buffering,
    Seeking,
    Ready,
    Ended,
    Failed,
};

enum class RepeatMode : uint8_t {
    Off,
    One,
};

enum class SeekMode : uint8_t {
    Fast,
    Exact,
};

// Identifies one seek. Pipelines and sources stamp frames with it and drop frames of older epochs.
using SeekEpoch = uint64_t;
inline constexpr SeekEpoch kInvalidSeekEpoch = 0;

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

// Player identity and the shared GL context the hardware video decoder needs.
struct PlayerContext {
    std::string playerId;
    std::shared_ptr<slark::IEGLContext> mainGLContext;
};

struct ComponentFactories {
    std::function<std::unique_ptr<AudioSink>(std::shared_ptr<AudioInfo>)> makeAudioSink;
    std::function<std::unique_ptr<VideoSink>()> makeVideoSink;
    std::function<std::shared_ptr<slark::IDecoder>(bool isAudio)> makeDecoder;
};

struct PlaybackFactory {
    using Wake = std::function<void()>;

    std::function<std::unique_ptr<Source>(const Prepare& prepare, SeekEpoch epoch, Wake wake)>
        createSource;
    std::function<std::vector<std::unique_ptr<Pipeline>>(Source& source,
                                                         const MediaInfo& info,
                                                         SeekEpoch epoch,
                                                         Wake wake)>
        createPipelines;
};

[[nodiscard]] PlaybackFactory makePlaybackFactory(PlayerSetting setting,
                                                  PlayerContext context,
                                                  ComponentFactories components);

}  // namespace slark::engine
