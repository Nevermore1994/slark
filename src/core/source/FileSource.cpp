//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "FileSource.h"

#include "StreamFactory.hpp"

namespace slark::engine {

std::unique_ptr<FileSource> FileSource::create(SourceOptions options, SeekEpoch epoch, Wake wake) {
    std::unique_ptr<FileSource> source(new FileSource(std::move(options), epoch, std::move(wake)));
    source->start();
    return source;
}

StreamResult<std::shared_ptr<IStream>> FileSource::createStream() {
    auto stream = StreamFactory::createFileStream(options_.uri);
    if (!stream) {
        return std::unexpected(Error::FileNotFound);
    }
    return std::move(*stream);
}

StreamResult<void> FileSource::seekStream(Time::TimePoint, uint64_t pos, uint64_t generation) {
    return stream_->seek(pos, generation);
}

}  // namespace slark::engine
