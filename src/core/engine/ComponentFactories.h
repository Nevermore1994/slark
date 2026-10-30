//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <functional>
#include <memory>

#include "AudioInfo.h"
#include "IDecoder.h"
#include "pipeline/AudioSink.h"
#include "pipeline/VideoSink.h"

namespace slark::engine {

struct ComponentFactories {
    std::function<std::unique_ptr<AudioSink>(std::shared_ptr<AudioInfo>)> makeAudioSink;
    std::function<std::unique_ptr<VideoSink>()> makeVideoSink;
    std::function<std::shared_ptr<slark::IDecoder>(bool isAudio)> makeDecoder;
};

}  // namespace slark::engine
