//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "PerfTypes.h"

namespace slark::perf {

class PlaySession;

/**
 * @brief Background resource sampler, periodically samples CPU/memory and pushes
 *        snapshots to active PlaySessions
 *
 * Thread safety: start/stop/register/unregister are all callable from any thread.
 */
class ResourceSampler {
 public:
    explicit ResourceSampler(uint32_t intervalMs = 1000);
    ~ResourceSampler();

    ResourceSampler(const ResourceSampler&) = delete;
    ResourceSampler& operator=(const ResourceSampler&) = delete;

    void start();
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;

    void registerSession(std::weak_ptr<PlaySession> session);
    void unregisterSession(const std::string& sessionId);

 private:
    void sampleLoop();
    ResourceSnapshot sampleOnce();
    double calcCpuUsage();
    void readMemoryInfo(int64_t& rssKB, int64_t& vmSizeKB);

    uint32_t intervalMs_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> thread_;
    std::condition_variable cv_;
    std::mutex cvMutex_;

    std::mutex sessionsMutex_;
    std::vector<std::weak_ptr<PlaySession>> activeSessions_;

    // Platform-specific CPU computation state
    int64_t prevProcTimeUs_{0};
    int64_t prevWallTimeUs_{0};

#if defined(__ANDROID__)
    int64_t prevProcTicks_{0};
    int64_t prevSysTicks_{0};
#endif
};

}  // namespace slark::perf
