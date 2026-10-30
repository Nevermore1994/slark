//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "HLSStream.h"
#include "StreamSource.h"

namespace slark::engine {

class HLSSource final : public StreamSource {
 public:
    static std::unique_ptr<HLSSource> create(SourceOptions options, SeekEpoch epoch, Wake wake);

    ~HLSSource() override {
        close();
    }

 private:
    using StreamSource::StreamSource;

    StreamResult<std::shared_ptr<IStream>> createStream() override;
    StreamResult<void> seekStream(Time::TimePoint target, uint64_t pos, SeekEpoch epoch) override;

    [[nodiscard]] bool seeksByTime() const override {
        return true;
    }

    [[nodiscard]] double mapPts(const AVFrame& frame) const noexcept override;
    [[nodiscard]] double playbackEndSec() const noexcept override;
    void onPacket(const DataPacket& packet) override;

    [[nodiscard]] double rangeStartSec() const noexcept {
        return options_.playbackRange.valid() ? options_.playbackRange.startSec : 0.0;
    }

    struct TrackTimeline {
        std::optional<double> anchorRawPts;
        double offsetSec = 0.0;
        std::optional<double> lastPlaybackSec;
        bool discontinuityPending = false;
    };

    double anchorSec_ = 0.0;
    // mutable: mapPts is const on the base interface; all writes happen on the loader.
    mutable TrackTimeline audioTimeline_;
    mutable TrackTimeline videoTimeline_;
    mutable uint64_t currentSequence_ = 0;
    mutable bool hasCurrentSequence_ = false;
    std::shared_ptr<slark::HLSStream> hls_;
};

}  // namespace slark::engine
