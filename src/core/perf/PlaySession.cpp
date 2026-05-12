//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlaySession.h"

#include "Log.hpp"
#include "Time.hpp"

#include <format>

namespace slark::perf {

PlaySession::PlaySession(std::string sessionId, std::string url)
    : sessionId_(std::move(sessionId)),
      url_(std::move(url)),
      startUs_(static_cast<int64_t>(Time::nowTimeStamp().point())) {}

void PlaySession::record(const std::string& stage, int64_t durationUs) {
    {
        std::shared_lock<std::shared_mutex> rlock(statsMutex_);
        auto it = stageStats_.find(stage);
        if (it != stageStats_.end()) {
            it->second.record(durationUs);
            return;
        }
    }

    std::unique_lock<std::shared_mutex> wlock(statsMutex_);
    阶段数量上限防护
    if (stageStats_.size() >= kMaxStageCount) {
        LogW("[Perf] stage count limit reached ({}), ignoring: {}", kMaxStageCount, stage);
        return;
    }
    auto [it, inserted] = stageStats_.try_emplace(stage);
    if (inserted) {
        it->second.name = stage;
    }
    it->second.record(durationUs);
}

void PlaySession::recordFrame(int64_t frameDurationUs) noexcept {
    frameStats_.record(frameDurationUs);
}

void PlaySession::recordEvent(const std::string& eventName, int64_t timestampUs) {
    std::lock_guard<std::mutex> lock(milestoneMutex_);
    milestones_.push_back(
        {eventName,
         timestampUs > 0 ? timestampUs : static_cast<int64_t>(Time::nowTimeStamp().point())});
}

void PlaySession::markMilestone(std::string_view label) {
    std::lock_guard<std::mutex> lock(milestoneMutex_);
    milestones_.push_back({std::string(label), static_cast<int64_t>(Time::nowTimeStamp().point())});
}

void PlaySession::printMilestones() const noexcept {
    std::vector<MilestoneEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(milestoneMutex_);
        snapshot = milestones_;
    }

    if (snapshot.empty()) {
        return;
    }

    std::string output("=== Player Startup Milestones ===
");
    for (const auto& entry : snapshot) {
        auto deltaMs = static_cast<double>(entry.timestampUs - startUs_) / 1000.0;
        output.append(std::format("  {}: {:.1f} ms
", entry.label, deltaMs));
    }
    auto totalMs = static_cast<double>(snapshot.back().timestampUs - startUs_) / 1000.0;
    output.append(std::format("  Total: {:.1f} ms
", totalMs));
    output.append("=================================
");
    LogI("{}", output);
}

void PlaySession::markFirstFrame() noexcept {
    int64_t expected = 0;
    int64_t now = static_cast<int64_t>(Time::nowTimeStamp().point());
    firstFrameUs_.compare_exchange_strong(expected, now, std::memory_order_relaxed);
}

void PlaySession::finish(bool success) {
    幂等保护：仅第一次调用生效
    bool expected = false;
    if (!finished_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    endUs_.store(static_cast<int64_t>(Time::nowTimeStamp().point()), std::memory_order_release);
    success_.store(success, std::memory_order_release);

    延迟 include 避免循环依赖，通过前向声明在.cc 中引入 PerfMonitor 上报在 PerfMonitor.cc 中通过
        reportSession 触发 此处通过全局单例调用，避免头文件循环 extern void
        perfMonitorReportSession(PlaySession&);
    perfMonitorReportSession(*this);
}

void PlaySession::onResourceSample(const ResourceSnapshot& snap) {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    resourceStats_.record(snap);
}

std::unordered_map<std::string, PerfStats> PlaySession::stageStatsSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(statsMutex_);
    return stageStats_;
}

FrameStats PlaySession::frameStatsSnapshot() const {
    return frameStats_;
}

ResourceStats PlaySession::resourceStatsSnapshot() const {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    return resourceStats_;
}

std::vector<MilestoneEntry> PlaySession::milestonesSnapshot() const {
    std::lock_guard<std::mutex> lock(milestoneMutex_);
    return milestones_;
}

double PlaySession::elapsedMs() const noexcept {
    int64_t end = endUs_.load(std::memory_order_acquire);
    if (end == 0) {
        end = static_cast<int64_t>(Time::nowTimeStamp().point());
    }
    return static_cast<double>(end - startUs_) / 1000.0;
}

double PlaySession::firstFrameMs() const noexcept {
    int64_t ts = firstFrameUs_.load(std::memory_order_relaxed);
    if (ts == 0) {
        return -1.0;
    }
    return static_cast<double>(ts - startUs_) / 1000.0;
}

} // namespace slark::perf
namespace slark::perf
