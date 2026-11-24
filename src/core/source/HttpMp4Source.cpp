//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HttpMp4Source.h"

#include "StreamFactory.hpp"

namespace slark::engine {

std::unique_ptr<HttpMp4Source> HttpMp4Source::create(SourceOptions options, SeekEpoch epoch, Wake wake) {
    std::unique_ptr<HttpMp4Source> source(
        new HttpMp4Source(std::move(options), epoch, std::move(wake)));
    source->start();
    return source;
}

StreamResult<std::shared_ptr<IStream>> HttpMp4Source::createStream() {
    auto stream = StreamFactory::createHttpStream(options_.uri, loader());
    if (!stream) {
        return std::unexpected(Error::SchemeNotSupported);
    }
    return std::move(*stream);
}

StreamResult<void> HttpMp4Source::seekStream(Time::TimePoint, uint64_t pos, uint64_t generation) {
    return stream_->seek(pos, generation);
}

void HttpMp4Source::applyFlowGate(bool closed) {
    if (closed) {
        stream_->closeFetchGate();
    } else {
        stream_->openFetchGate();
    }
}

}  // namespace slark::engine
