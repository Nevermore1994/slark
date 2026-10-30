//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <memory>

#include "StreamSource.h"

namespace slark::engine {

class HttpMp4Source final : public StreamSource {
 public:
    static std::unique_ptr<HttpMp4Source> create(SourceOptions options, SeekEpoch epoch, Wake wake);

    ~HttpMp4Source() override {
        close();
    }

 private:
    using StreamSource::StreamSource;

    StreamResult<std::shared_ptr<IStream>> createStream() override;
    StreamResult<void> seekStream(Time::TimePoint target, uint64_t pos, uint64_t generation) override;
    void applyFlowGate(bool closed) override;

    // One Range request has no boundary to pull the gate at; per-chunk wakeups undo its pause.
    [[nodiscard]] bool pullsGate() const override {
        return false;
    }
};

}  // namespace slark::engine
