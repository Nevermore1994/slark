//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <memory>

#include "StreamSource.h"

namespace slark::engine {

class FileSource final : public StreamSource {
 public:
    static std::unique_ptr<FileSource> create(SourceOptions options, SeekEpoch epoch, Wake wake);

    ~FileSource() override {
        close();
    }

 private:
    using StreamSource::StreamSource;

    StreamResult<std::shared_ptr<IStream>> createStream() override;
    StreamResult<void> seekStream(Time::TimePoint target, uint64_t pos, uint64_t generation) override;
};

}  // namespace slark::engine
