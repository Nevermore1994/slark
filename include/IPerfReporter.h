//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <memory>

#include "PerfTypes.h"

namespace slark::perf {

struct FrameTrace;

struct IPerfReporter {
    /**
     * @brief Frame-trace completion callback
     * @param trace read-only frame-trace snapshot
     */
    virtual void onFrameTrace(std::shared_ptr<const FrameTrace> trace) = 0;

    /**
     * @brief Playback session completion callback
     * @param report full session report data: first-frame latency, stage stats, resource
     *               stats, etc. (read-only, must not be modified)
     */
    virtual void onSessionComplete(std::shared_ptr<const SessionReport> report) = 0;

    virtual ~IPerfReporter() = default;
};

}  // namespace slark::perf
