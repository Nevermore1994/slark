//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>

#include "IStream.h"

namespace slark::pipeline {

enum class MediaSourceType : uint8_t {
    Local,
    Network,
    Live,
};

[[nodiscard]] inline MediaSourceType sourceTypeOf(StreamType type, bool isLive) noexcept {
    if (isLive) {
        return MediaSourceType::Live;
    }
    if (type == StreamType::Http || type == StreamType::HLS) {
        return MediaSourceType::Network;
    }
    return MediaSourceType::Local;
}

}
