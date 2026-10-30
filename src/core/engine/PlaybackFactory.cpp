//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "PlaybackFactory.h"

#include <utility>

#include "DecoderBuilder.h"
#include "PipelineFactory.h"
#include "SourceFactory.h"

namespace slark::engine {

PlaybackFactory makePlaybackFactory(PlayerSetting setting,
                                    PlayerContext context,
                                    ComponentFactories components) {
    DecoderBuilder decoders(std::move(setting), std::move(context), components.makeDecoder);
    auto pipelines = std::make_shared<const PipelineFactory>(std::move(components),
                                                             std::move(decoders));
    PlaybackFactory factory;
    factory.createSource = &SourceFactory::create;
    factory.createPipelines = [pipelines](Source& source, const MediaInfo&, SeekEpoch epoch,
                                          PlaybackFactory::Wake wake) {
        return pipelines->create(source, epoch, std::move(wake));
    };
    return factory;
}

}  // namespace slark::engine
