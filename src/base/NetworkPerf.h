//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace slark::perf {

// Neutral DTO shared by the HTTP layer and the perf subsystem; neither side
// includes the other's headers.
struct NetworkSample {
    // Playback session owning this request; empty means unattributed.
    std::string perfSessionId;
    std::string host;
    std::string connectResult;
    std::string parseResult;
    std::string timeoutSource;
    int64_t connectMs = 0;
    int64_t tlsHandshakeMs = 0;
    int64_t totalRequestMs = 0;
    int64_t sendMs = 0;
    int64_t receiveMs = 0;
    int64_t headerParseUs = 0;
    uint64_t bodyBytes = 0;
    int32_t chunkCount = 0;
    int32_t redirectCount = 0;
    int32_t recvLoops = 0;
    int32_t sendRetryCount = 0;
    int32_t receiveWaitCount = 0;
    bool success = false;
    bool connectionReused = false;
};

struct NetworkStats {
    uint64_t requestCount = 0;
    uint64_t successCount = 0;
    uint64_t failureCount = 0;
    uint64_t totalBodyBytes = 0;
    int64_t totalRequestMs = 0;
    int64_t peakRequestMs = 0;
    NetworkSample lastSample;

    void record(const NetworkSample& sample) {
        ++requestCount;
        if (sample.success) {
            ++successCount;
        } else {
            ++failureCount;
        }
        totalBodyBytes += sample.bodyBytes;
        totalRequestMs += sample.totalRequestMs;
        if (sample.totalRequestMs > peakRequestMs) {
            peakRequestMs = sample.totalRequestMs;
        }
        lastSample = sample;
    }

    [[nodiscard]] double avgRequestMs() const noexcept {
        return requestCount > 0 ? static_cast<double>(totalRequestMs) /
                                      static_cast<double>(requestCount)
                                : 0.0;
    }
};

using NetworkSampleSink = std::function<void(const NetworkSample&)>;

// Process-lifetime storage: PerfMonitor's destructor clears the sink during static
// teardown, so these must outlive every static object that touches them.
inline std::mutex& networkSinkMutex() noexcept {
    static auto* mutex = new std::mutex;
    return *mutex;
}

inline NetworkSampleSink& networkSink() noexcept {
    static auto* sink = new NetworkSampleSink;
    return *sink;
}

inline void setNetworkSampleSink(NetworkSampleSink sink) {
    std::lock_guard<std::mutex> lock(networkSinkMutex());
    networkSink() = std::move(sink);
}

inline void publishNetworkSample(const NetworkSample& sample) {
    NetworkSampleSink sink;
    {
        std::lock_guard<std::mutex> lock(networkSinkMutex());
        sink = networkSink();
    }
    if (!sink) {
        return;
    }
    try {
        sink(sample);
    } catch (...) {
        // A failing sink must never break the request coroutine.
    }
}

}  // namespace slark::perf
