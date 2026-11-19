//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <string>

namespace slark {

// Grouped "label: value unit" lines. Each group is produced inside its owning
// component's lock, so no caller ever reads thread-confined state directly.
struct DebugInfo {
    std::string source;
    std::string video;
    std::string audio;
    std::string playback;
    std::string sync;
    std::string buffer;
    std::string queue;
    std::string render;
    std::string network;
    std::string system;

    void append(const DebugInfo& other) {
        source += other.source;
        video += other.video;
        audio += other.audio;
        playback += other.playback;
        sync += other.sync;
        buffer += other.buffer;
        queue += other.queue;
        render += other.render;
        network += other.network;
        system += other.system;
    }
};

[[nodiscard]] std::string formatDebugInfo(const DebugInfo& info);

}  // namespace slark
