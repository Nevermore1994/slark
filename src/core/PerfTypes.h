//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace slark::perf {

// ─── 配置 ────────────────────────────────────────────────────────────────────

struct PerfConfig {
    bool enabled = false;
    bool enableFrameTrace = false;
    bool enableSessionTrace = true;
    bool enableResourceSampling = false;

    double slowThresholdMs = 16.0;
    uint32_t resourceIntervalMs = 1000;
};

struct DeviceEnv {
    std::string deviceModel;
    std::string osVersion;
    std::string appVersion;
    std::string networkType;
};

// ─── 统计 ────────────────────────────────────────────────────────────────────

struct PerfStats {
    std::string name;
    uint64_t count = 0;
    int64_t totalUs = 0;
    int64_t minUs = INT64_MAX;
    int64_t maxUs = 0;

    void record(int64_t durationUs) noexcept {
        ++count;
        totalUs += durationUs;
        if (durationUs < minUs) {
            minUs = durationUs;
        }
        if (durationUs > maxUs) {
            maxUs = durationUs;
        }
    }

    [[nodiscard]] double avgMs() const noexcept {
        return count > 0 ? static_cast<double>(totalUs) / static_cast<double>(count) / 1000.0 : 0.0;
    }
};

struct FrameStats {
    std::atomic<uint64_t> totalFrames{0};
    std::atomic<uint64_t> droppedFrames{0};
    std::atomic<int64_t> totalDurationUs{0};

    FrameStats() = default;

    FrameStats(const FrameStats& other) noexcept
        : totalFrames(other.totalFrames.load(std::memory_order_relaxed)),
          droppedFrames(other.droppedFrames.load(std::memory_order_relaxed)),
          totalDurationUs(other.totalDurationUs.load(std::memory_order_relaxed)) {}

    FrameStats& operator=(const FrameStats& other) noexcept {
        if (this != &other) {
            totalFrames.store(other.totalFrames.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
            droppedFrames.store(other.droppedFrames.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
            totalDurationUs.store(other.totalDurationUs.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
        }
        return *this;
    }

    void record(int64_t frameDurationUs) noexcept {
        ++totalFrames;
        totalDurationUs.fetch_add(frameDurationUs, std::memory_order_relaxed);
    }

    void recordDrop() noexcept {
        ++droppedFrames;
    }

    [[nodiscard]] double avgFps() const noexcept {
        auto dur = totalDurationUs.load(std::memory_order_relaxed);
        return dur > 0 ? static_cast<double>(totalFrames.load()) / (static_cast<double>(dur) / 1e6)
                       : 0.0;
    }
};

struct ResourceSnapshot {
    double cpuPercent = 0.0;
    int64_t rssKB = 0;
    int64_t vmSizeKB = 0;
    int64_t timestampUs = 0;
};

struct ResourceStats {
    uint64_t sampleCount = 0;
    double avgCpuPercent = 0.0;
    double peakCpuPercent = 0.0;
    int64_t peakRssKB = 0;

    void record(const ResourceSnapshot& snap) noexcept {
        ++sampleCount;
        avgCpuPercent = (avgCpuPercent * (sampleCount - 1) + snap.cpuPercent) / sampleCount;
        if (snap.cpuPercent > peakCpuPercent) {
            peakCpuPercent = snap.cpuPercent;
        }
        if (snap.rssKB > peakRssKB) {
            peakRssKB = snap.rssKB;
        }
    }
};

struct MilestoneEntry {
    std::string label;
    int64_t timestampUs = 0;
};

struct SlowOpsStats {
    double thresholdMs = 16.0;
    uint64_t totalCount = 0;
    std::unordered_map<std::string, uint64_t> countByStage;

    void record(const std::string& stage, double durationMs) noexcept {
        if (durationMs >= thresholdMs) {
            ++totalCount;
            ++countByStage[stage];
        }
    }
};

// ─── 会话报告 ─────────────────────────────────────────────────────────────────

struct SessionReport {
    std::string sessionId;
    std::string url;
    double elapsedMs = 0.0;
    double firstFrameMs = -1.0;
    bool success = false;

    std::vector<MilestoneEntry> milestones;
    std::unordered_map<std::string, PerfStats> stageStats;
    FrameStats frameStats;
    ResourceStats resourceStats;
    DeviceEnv deviceEnv;
};

// ─── 帧追踪 ───────────────────────────────────────────────────────────────────

struct SpanEntry {
    std::string name;
    int64_t startUs = 0;
    int64_t endUs = 0;

    [[nodiscard]] int64_t durationUs() const noexcept {
        return endUs - startUs;
    }
};

struct FrameTrace {
    uint64_t traceId = 0;
    int64_t pts = 0;
    std::vector<SpanEntry> spans;

    void addSpan(std::string name, int64_t startUs, int64_t endUs) {
        spans.push_back({std::move(name), startUs, endUs});
    }
};

/**
 * @brief RAII span，析构时自动记录结束时间
 */
class ScopedSpan {
public:
    ScopedSpan(FrameTrace& trace, std::string name)
        : trace_(trace), name_(std::move(name)), startUs_(nowUs()) {}

    ~ScopedSpan() {
        trace_.addSpan(std::move(name_), startUs_, nowUs());
    }

    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;

private:
    static int64_t nowUs() noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    FrameTrace& trace_;
    std::string name_;
    int64_t startUs_;
};

} // namespace slark::perf
