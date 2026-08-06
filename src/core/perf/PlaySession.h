//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "PerfTypes.h"
#include "Time.hpp"

namespace slark::perf {

/**
 * @brief Performance tracking for one complete playback session
 *
 * Lifecycle: play -> (record/markMilestone/recordFrame) -> finish
 * finish() is idempotent; only the first call takes effect.
 */
class PlaySession : public std::enable_shared_from_this<PlaySession> {
 public:
    static constexpr size_t kMaxStageCount = 128;

    /**
     * @param sessionId unique session ID (allocated by PerfMonitor)
     * @param url       playback resource identifier
     */
    PlaySession(std::string sessionId, std::string url);

    // Record a stage duration; dropped with a warning once kMaxStageCount is reached
    void record(const std::string& stage, int64_t durationUs);

    // Accumulate frame rendering stats
    void recordFrame(int64_t frameDurationUs) noexcept;

    // Record a lifecycle event timestamp
    void recordEvent(const std::string& eventName, int64_t timestampUs);

    // Record a milestone (key nodes only; do not call for unimportant ones)
    void markMilestone(std::string_view label);

    // Log all milestone durations relative to session start (replaces DebugInfo::print)
    void printMilestones() const noexcept;

    // Record the first-frame render time; only the first call takes effect
    void markFirstFrame() noexcept;

    // Mark the session end and trigger the PerfMonitor report (idempotent)
    void finish(bool success);

    // ResourceSampler callback, pushing a resource sample snapshot
    void onResourceSample(const ResourceSnapshot& snap);

    [[nodiscard]] std::unordered_map<std::string, PerfStats> stageStatsSnapshot() const;
    [[nodiscard]] FrameStats frameStatsSnapshot() const;
    [[nodiscard]] ResourceStats resourceStatsSnapshot() const;
    [[nodiscard]] std::vector<MilestoneEntry> milestonesSnapshot() const;

    [[nodiscard]] double elapsedMs() const noexcept;
    [[nodiscard]] double firstFrameMs() const noexcept;

    [[nodiscard]] const std::string& sessionId() const noexcept {
        return sessionId_;
    }

    [[nodiscard]] const std::string& url() const noexcept {
        return url_;
    }

    [[nodiscard]] int64_t startUs() const noexcept {
        return startUs_;
    }

    [[nodiscard]] bool isSuccess() const noexcept {
        return success_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isFinished() const noexcept {
        return finished_.load(std::memory_order_acquire);
    }

 private:
    std::string sessionId_;
    std::string url_;
    int64_t startUs_{0};
    std::atomic<int64_t> endUs_{0};
    std::atomic<int64_t> firstFrameUs_{0};
    // 0 means first frame was never reached
    std::atomic<bool> success_{false};
    std::atomic<bool> finished_{false};

    mutable std::shared_mutex statsMutex_;
    std::unordered_map<std::string, PerfStats> stageStats_;

    FrameStats frameStats_;

    mutable std::mutex milestoneMutex_;
    std::vector<MilestoneEntry> milestones_;

    mutable std::mutex resourceMutex_;
    ResourceStats resourceStats_;
};

}  // namespace slark::perf
