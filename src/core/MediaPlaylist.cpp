//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "MediaPlaylist.h"

namespace slark {

void MediaPlaylist::addSegment(Segment segment) {
    segment.index = static_cast<uint32_t>(segments_.size());
    segments_.push_back(std::move(segment));
}

void MediaPlaylist::addVariant(Variant variant) {
    variants_.push_back(std::move(variant));
}

std::optional<uint32_t> MediaPlaylist::findSegmentByTime(double time) const noexcept {
    if (segments_.empty()) {
        return std::nullopt;
    }

    auto it = std::lower_bound(segments_.begin(), segments_.end(), time,
                               [](const Segment& seg, double t) {
                                   return seg.startTime < t;
                               });

    if (it != segments_.begin()) {
        --it;
        if (time >= it->startTime && time < it->endTime()) {
            return it->index;
        }
        ++it;
    }

    if (it != segments_.end() && time >= it->startTime && time < it->endTime()) {
        return it->index;
    }

    return std::nullopt;
}

std::optional<uint32_t> MediaPlaylist::findSegmentByIndex(uint32_t index) const noexcept {
    if (index >= segments_.size()) {
        return std::nullopt;
    }
    return index;
}

}  // namespace slark
