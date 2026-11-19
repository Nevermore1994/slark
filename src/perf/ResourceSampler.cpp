//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "ResourceSampler.h"

#include <algorithm>

#include "PlaySession.h"
#include "ProcessMetrics.h"
#include "Time.hpp"

namespace slark::perf {

ResourceSampler::ResourceSampler(uint32_t intervalMs) : intervalMs_(intervalMs) {}

ResourceSampler::~ResourceSampler() {
    stop();
}

void ResourceSampler::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::make_unique<std::thread>(&ResourceSampler::sampleLoop, this);
}

void ResourceSampler::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(cvMutex_);
        cv_.notify_one();
    }
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();
}

bool ResourceSampler::isRunning() const noexcept {
    return running_.load(std::memory_order_relaxed);
}

void ResourceSampler::registerSession(std::weak_ptr<PlaySession> session) {
    auto s = session.lock();
    if (!s) {
        return;
    }
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (const auto& weak : activeSessions_) {
        if (auto existing = weak.lock(); existing && existing->sessionId() == s->sessionId()) {
            return;
        }
    }
    activeSessions_.push_back(std::move(session));
}

void ResourceSampler::unregisterSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    activeSessions_.erase(std::remove_if(activeSessions_.begin(), activeSessions_.end(),
                                         [&sessionId](auto& weak) {
                                             auto s = weak.lock();
                                             return !s || s->sessionId() == sessionId;
                                         }),
                          activeSessions_.end());
}

void ResourceSampler::sampleLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        auto snap = sampleOnce();

        // Copy the snapshot first, then call back after releasing the lock,
        // avoiding heavy work while holding the lock
        std::vector<std::shared_ptr<PlaySession>> liveSessions;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            std::vector<std::weak_ptr<PlaySession>> alive;
            for (auto& weak : activeSessions_) {
                if (auto s = weak.lock()) {
                    liveSessions.push_back(s);
                    alive.push_back(weak);
                }
            }
            activeSessions_ = std::move(alive);
        }
        for (auto& s : liveSessions) {
            s->onResourceSample(snap);
        }

        std::unique_lock<std::mutex> lock(cvMutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(intervalMs_), [this] {
            return !running_.load(std::memory_order_relaxed);
        });
    }
}

ResourceSnapshot ResourceSampler::sampleOnce() {
    ResourceSnapshot snap;
    snap.timestampUs = Time::systemNowUs();
    snap.cpuPercent = sampleCpuPercent(cpuBaseline_);
    readProcessMemoryKB(snap.rssKB, snap.vmSizeKB);
    return snap;
}

}  // namespace slark::perf
