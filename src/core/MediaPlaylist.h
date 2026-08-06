//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Range.h"

namespace slark {

struct Segment {
    uint32_t index = 0;
    double duration = 0.0;
    double startTime = 0.0;
    std::string uri;
    std::optional<Range> byteRange;
    bool isDiscontinuity = false;

    [[nodiscard]] double endTime() const noexcept {
        return startTime + duration;
    }
};

struct Variant {
    uint32_t bandwidth = 0;
    std::string uri;
    std::string name;
    std::optional<uint32_t> width;
    std::optional<uint32_t> height;
};

class MediaPlaylist {
 public:
    [[nodiscard]] const std::vector<Segment>& segments() const noexcept {
        return segments_;
    }

    [[nodiscard]] size_t segmentCount() const noexcept {
        return segments_.size();
    }

    /// Return the matching segment index, or std::nullopt when there is no match
    [[nodiscard]] std::optional<uint32_t> findSegmentByTime(double time) const noexcept;

    /// Return the matching segment index, or std::nullopt when there is no match
    [[nodiscard]] std::optional<uint32_t> findSegmentByIndex(uint32_t index) const noexcept;

    [[nodiscard]] const std::vector<Variant>& variants() const noexcept {
        return variants_;
    }

    [[nodiscard]] bool hasVariants() const noexcept {
        return !variants_.empty();
    }

    [[nodiscard]] double totalDuration() const noexcept {
        return totalDuration_;
    }

    [[nodiscard]] bool isLive() const noexcept {
        return isLive_;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return !segments_.empty() || !variants_.empty();
    }

    void addSegment(Segment segment);

    void addVariant(Variant variant);

    void setTotalDuration(double duration) {
        totalDuration_ = duration;
    }

    void setLive(bool live) {
        isLive_ = live;
    }

 private:
    std::vector<Segment> segments_;
    std::vector<Variant> variants_;
    double totalDuration_ = 0.0;
    bool isLive_ = false;
};

}  // namespace slark
