//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <functional>
#include <memory>

#include "AVFrame.hpp"
#include "AudioInfo.h"
#include "Time.hpp"

namespace slark::engine {

class AudioSink {
 public:
    virtual ~AudioSink() = default;

    virtual bool open(std::shared_ptr<AudioInfo> info) = 0;

    // Returns false when the buffer lacks room for the whole frame; the pipeline retries later.
    virtual bool write(AVFrameRefPtr frame) = 0;

    // offset + consumed − hardware latency, monotonic.
    [[nodiscard]] virtual Time::TimePoint position() const = 0;

    // Buffered audio covers the prefill threshold, i.e. starting playback will not underrun.
    [[nodiscard]] virtual bool primed() const = 0;

    virtual void start() = 0;
    virtual void pause() = 0;
    virtual void setVolume(float volume) {
        (void)volume;
    }

    // Drops buffered audio and rebases position to `offset`.
    virtual void flush(Time::TimePoint offset) = 0;

    // No more PCM will come; drained() becomes true once the buffer and hardware tail played out.
    virtual void markDecoderEnded() = 0;

    [[nodiscard]] virtual bool drained() const = 0;
};

}  // namespace slark::engine
