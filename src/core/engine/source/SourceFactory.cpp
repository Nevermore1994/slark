//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "SourceFactory.h"

#include <utility>

#include "FileSource.h"
#include "HLSSource.h"
#include "HttpMp4Source.h"
#include "StreamFactory.hpp"

namespace slark::engine {

std::unique_ptr<Source> SourceFactory::create(const Prepare& prepare,
                                              SeekEpoch epoch,
                                              Wake wake) {
    SourceOptions options;
    options.uri = prepare.item.path;
    options.playbackRange = prepare.playbackRange;
    options.flow = prepare.setting.flowConfig.value_or(FlowConfig{});
    options.perfSessionId = prepare.perfSessionId;
    switch (StreamFactory::detectStreamType(options.uri)) {
        case StreamType::HLS:
            options.demuxer = DemuxerType::TS;
            return HLSSource::create(std::move(options), epoch, std::move(wake));
        case StreamType::Http:
            return HttpMp4Source::create(std::move(options), epoch, std::move(wake));
        case StreamType::File:
            return FileSource::create(std::move(options), epoch, std::move(wake));
        default:
            return nullptr;
    }
}

}  // namespace slark::engine
