//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "AudioInfo.h"
#include "ComponentFactories.h"
#include "DecoderBuilder.h"
#include "Pipeline.h"
#include "SeekEpoch.h"
#include "Source.h"
#include "VideoInfo.h"

namespace slark::engine {

class PipelineFactory {
 public:
    using Wake = std::function<void()>;

    PipelineFactory(ComponentFactories components, DecoderBuilder decoders);

    // Returns none when any track cannot be built: a half-built playback never becomes ready.
    [[nodiscard]] std::vector<std::unique_ptr<Pipeline>> create(Source& source,
                                                                SeekEpoch epoch,
                                                                Wake wake) const;

 private:
    [[nodiscard]] std::unique_ptr<Pipeline> createAudio(
        Source& source,
        const std::shared_ptr<AudioInfo>& info,
        SeekEpoch epoch,
        const Wake& wake) const;
    [[nodiscard]] std::unique_ptr<Pipeline> createVideo(
        Source& source,
        const std::shared_ptr<VideoInfo>& info,
        SeekEpoch epoch,
        const Wake& wake) const;

    ComponentFactories components_;
    DecoderBuilder decoders_;
};

}  // namespace slark::engine
