//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "PipelineFactory.h"

#include <utility>

#include "AudioPipeline.h"
#include "VideoPipeline.h"

namespace slark::engine {

PipelineFactory::PipelineFactory(ComponentFactories components, DecoderBuilder decoders)
    : components_(std::move(components)), decoders_(std::move(decoders)) {}

std::vector<std::unique_ptr<Pipeline>> PipelineFactory::create(Source& source,
                                                               SeekEpoch epoch,
                                                               Wake wake) const {
    std::vector<std::unique_ptr<Pipeline>> pipelines;
    if (const auto info = source.audioTrackInfo()) {
        auto pipeline = createAudio(source, info, epoch, wake);
        if (!pipeline) {
            return {};
        }
        pipelines.push_back(std::move(pipeline));
    }
    if (const auto info = source.videoTrackInfo()) {
        auto pipeline = createVideo(source, info, epoch, wake);
        if (!pipeline) {
            return {};
        }
        pipelines.push_back(std::move(pipeline));
    }
    return pipelines;
}

std::unique_ptr<Pipeline> PipelineFactory::createAudio(Source& source,
                                                       const std::shared_ptr<AudioInfo>& info,
                                                       SeekEpoch epoch,
                                                       const Wake& wake) const {
    auto* receiver = source.frames(TrackType::Audio);
    auto sink = components_.makeAudioSink ? components_.makeAudioSink(info) : nullptr;
    auto decoder = receiver && sink ? decoders_.createAudio(info) : nullptr;
    if (!receiver || !sink || !decoder) {
        return nullptr;
    }
    return std::make_unique<AudioPipeline>(
        *receiver,
        [decoder = std::move(decoder)] {
            return decoder;
        },
        std::move(sink), epoch, Time::TimePoint{}, wake);
}

std::unique_ptr<Pipeline> PipelineFactory::createVideo(Source& source,
                                                       const std::shared_ptr<VideoInfo>& info,
                                                       SeekEpoch epoch,
                                                       const Wake& wake) const {
    auto* receiver = source.frames(TrackType::Video);
    auto sink = components_.makeVideoSink ? components_.makeVideoSink() : nullptr;
    auto decoder = receiver && sink ? decoders_.createVideo(info) : nullptr;
    if (sink) {
        // Rotation lives on the render side; without this rotated streams show sideways.
        sink->notifyVideoInfo(info);
    }
    if (!receiver || !sink || !decoder) {
        return nullptr;
    }
    return std::make_unique<VideoPipeline>(
        *receiver,
        [decoder = std::move(decoder)] {
            return decoder;
        },
        std::move(sink), VideoPipeline::Config{}, epoch, Time::TimePoint{}, wake);
}

}  // namespace slark::engine
