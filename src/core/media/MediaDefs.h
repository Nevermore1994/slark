//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once
#include <string_view>

namespace slark {

extern const std::string_view MEDIA_MIMETYPE_UNKNOWN;

extern const std::string_view MEDIA_MIMETYPE_IMAGE_JPEG;

extern const std::string_view MEDIA_MIMETYPE_VIDEO_AVC;
extern const std::string_view MEDIA_MIMETYPE_VIDEO_HEVC;

extern const std::string_view MEDIA_MIMETYPE_AUDIO_AAC;
extern const std::string_view MEDIA_MIMETYPE_AUDIO_RAW;
extern const std::string_view MEDIA_MIMETYPE_AUDIO_AAC_ADTS;

enum class TrackType {
    Unknown = 0,
    Audio = 1,
    Video = 2,
};

enum class RenderStatus {
    Unknown,
    Ready,
    Playing,
    Pause,
    Stop,
    Error,
};

}  // namespace slark
