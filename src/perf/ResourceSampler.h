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
#include "ProcessMetrics.h"

namespace slark::perf {

class PlaySession;

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

    uint32_t intervalMs_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> thread_;
    std::condition_variable cv_;
    std::mutex cvMutex_;

    std::mutex sessionsMutex_;
    std::vector<std::weak_ptr<PlaySession>> activeSessions_;

    CpuBaseline cpuBaseline_;
};

}  // namespace slark::perf
