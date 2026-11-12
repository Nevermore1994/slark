//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <string>
#include <vector>

namespace slark {

// Loads ahead of the playback position: stops at the high watermark, resumes below the low one.
struct SourceWatermarkConfig {
    double lowWatermarkSec = 3.0;
    double highWatermarkSec = 10.0;

    [[nodiscard]] bool isValid() const noexcept {
        return lowWatermarkSec >= 0.0 && lowWatermarkSec < highWatermarkSec;
    }
};

struct BufferingConfig {
    double startBufferSec = 2.0;
    double rebufferEnterSec = 3.0;
    double rebufferExitSec = 4.5;

    [[nodiscard]] bool isValid() const noexcept {
        return startBufferSec >= 0.0 && rebufferEnterSec >= 0.0 &&
               rebufferExitSec >= rebufferEnterSec;
    }
};

struct DnsConfig {
    std::vector<std::string> preResolveUrls;
    double cacheTtlSec = 60.0;

    [[nodiscard]] bool isValid() const noexcept {
        return cacheTtlSec > 0.0;
    }
};

struct FlowConfig {
    SourceWatermarkConfig source;
    BufferingConfig buffering;
    DnsConfig dns;

    [[nodiscard]] bool isValid() const noexcept {
        return source.isValid() && buffering.isValid() && dns.isValid() &&
               buffering.rebufferExitSec < source.highWatermarkSec;
    }
};

}  // namespace slark
