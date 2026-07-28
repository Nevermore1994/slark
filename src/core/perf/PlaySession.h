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
 * @brief 一次完整播放会话的性能跟踪
 *
 * 生命周期：play → (record/markMilestone/recordFrame) → finish
 * finish() 保证幂等，多次调用只有第一次生效。
 */
class PlaySession : public std::enable_shared_from_this<PlaySession> {
 public:
    static constexpr size_t kMaxStageCount = 128;

    /**
     * @param sessionId 唯一会话 ID（由 PerfMonitor 分配）
     * @param url       播放资源标识
     */
    PlaySession(std::string sessionId, std::string url);

    // 记录阶段耗时，超过 kMaxStageCount 时丢弃并打印警告
    void record(const std::string& stage, int64_t durationUs);

    // 累计帧渲染统计
    void recordFrame(int64_t frameDurationUs) noexcept;

    // 记录生命周期事件时间点
    void recordEvent(const std::string& eventName, int64_t timestampUs);

    // 打里程碑（仅记录关键节点，不重要的节点不要调用）
    void markMilestone(std::string_view label);

    // 以日志输出所有里程碑相对会话起始时间的耗时（替代 DebugInfo::print）
    void printMilestones() const noexcept;

    // 记录首帧渲染时刻，重复调用只有第一次生效
    void markFirstFrame() noexcept;

    // 标记会话结束，触发 PerfMonitor 上报（幂等）
    void finish(bool success);

    // ResourceSampler 回调，推送资源采样快照
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
    // 0 表示未到达首帧
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
