//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <thread>
#include <vector>

#include "AVFrame.hpp"
#include "Channel.hpp"
#include "Source.h"

namespace slark::engine::fake {

inline bool waitUntil(const std::function<bool()>& cond,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(8000)) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return cond();
}

struct Drained {
    std::vector<AVFrameRefPtr> audio;
    std::vector<AVFrameRefPtr> video;

    [[nodiscard]] static bool hasEos(const std::vector<AVFrameRefPtr>& v) {
        return !v.empty() && v.back()->isEOS;
    }
};

inline void drainInto(Source& source, Drained& out) {
    if (auto* rx = source.frames(TrackType::Audio)) {
        for (auto& f : rx->tryReceiveAll()) {
            out.audio.push_back(std::move(f));
        }
    }
    if (auto* rx = source.frames(TrackType::Video)) {
        for (auto& f : rx->tryReceiveAll()) {
            out.video.push_back(std::move(f));
        }
    }
}

// Plays the role of a consumer: keeps the clock at the end of what was produced so the watermark
// never closes, and drains the channels.
inline bool consume(Source& source, Drained& out) {
    source.regulate(source.cacheEnd());
    drainInto(source, out);
    return true;
}

inline bool isKey(const AVFrame& f) {
    return f.info && f.info->isKeyFrame();
}

}  // namespace slark::engine::fake
