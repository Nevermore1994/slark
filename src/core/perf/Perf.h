//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#ifdef SLARK_PERF_ENABLED

#include <optional>

#include "PerfMonitor.h"
#include "PerfTypes.h"
#include "PlaySession.h"

#define SLARK_PERF_CONCAT_INNER(a, b) a##b
#define SLARK_PERF_CONCAT(a, b) SLARK_PERF_CONCAT_INNER(a, b)

// Create a ScopedSpan directly from a FrameTrace pointer
#define SLARK_TRACE_SPAN(tracePtr, spanName)                                                    \
    std::optional<::slark::perf::ScopedSpan> SLARK_PERF_CONCAT(_slark_span_, __LINE__);         \
    do {                                                                                        \
        if (::slark::perf::PerfMonitor::instance().isFrameTraceEnabled()) {                     \
            auto* _ft = (tracePtr);                                                             \
            if (_ft) {                                                                          \
                SLARK_PERF_CONCAT(_slark_span_, __LINE__).emplace(*_ft, std::string(spanName)); \
            }                                                                                   \
        }                                                                                       \
    } while (0)

    // Extract FrameTrace from an object with a frameTrace() method and create a ScopedSpan
#define SLARK_TRACE_NODE(data, nodeName)                                                        \
    std::optional<::slark::perf::ScopedSpan> SLARK_PERF_CONCAT(_slark_node_, __LINE__);         \
    do {                                                                                        \
        if (::slark::perf::PerfMonitor::instance().isFrameTraceEnabled() && (data)) {           \
            auto* _ft = (data)->frameTrace();                                                   \
            if (_ft) {                                                                          \
                SLARK_PERF_CONCAT(_slark_node_, __LINE__).emplace(*_ft, std::string(nodeName)); \
            }                                                                                   \
        }                                                                                       \
    } while (0)

    // Record a milestone on a PlaySession pointer
#define SLARK_TRACE_MILESTONE(session, label)                                  \
    do {                                                                       \
        if (::slark::perf::PerfMonitor::instance().isEnabled() && (session)) { \
            (session)->markMilestone(label);                                   \
        }                                                                      \
    } while (0)

    // Record a stage duration on a PlaySession pointer
#define SLARK_TRACE_RECORD(session, stage, durationUs)                                     \
    do {                                                                                   \
        if (::slark::perf::PerfMonitor::instance().isSessionTraceEnabled() && (session)) { \
            (session)->record(stage, durationUs);                                          \
        }                                                                                  \
    } while (0)

#else  // SLARK_PERF_ENABLED not defined

#define SLARK_TRACE_SPAN(tracePtr, spanName) (void)0
#define SLARK_TRACE_NODE(data, nodeName) (void)0
#define SLARK_TRACE_MILESTONE(session, label) (void)0
#define SLARK_TRACE_RECORD(session, stage, durationUs) (void)0

#endif  // SLARK_PERF_ENABLED
