//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "IPerfReporter.h"
#include "PerfTypes.h"
#include "ResourceSampler.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace slark::perf {

class PlaySession;

/**
 * @brief 性能监控全局单例，统一管理 perf 配置、会话注册和数据上报
 */
class PerfMonitor {
public:
    static PerfMonitor& instance();

    / 重新配置 perf 系统：停止旧采样器、重置统计数据，但保留已注册的 reporter void configure(
          const PerfConfig& config);

    / 注入设备环境信息，会随每次 SessionReport 一起上报 void setDeviceEnv(const DeviceEnv& env);

    / 完全停机：停止采样、清空 reporter 和所有统计数据 void shutdown();

    void addReporter(std::shared_ptr<IPerfReporter> reporter);
    void removeReporter(const std::shared_ptr<IPerfReporter>& reporter);

    / 注册 /
        注销 PlaySession 到 ResourceSampler void registerSession(
            std::shared_ptr<PlaySession> session);
    void unregisterSession(const std::string& sessionId);

    / 分发帧跟踪快照给所有 reporter void reportFrame(std::shared_ptr<const FrameTrace> trace);

    / 构建 SessionReport 并分发给所有 reporter void reportSession(PlaySession& session);

    void recordSlowOp(const std::string& stage, double durationMs);
    void recordError(const std::string& reason);

    [[nodiscard]] SlowOpsStats slowOpsSnapshot() const;
    [[nodiscard]] std::unordered_map<std::string, uint64_t> errorsSnapshot() const;

    [[nodiscard]] bool isEnabled() const noexcept;
    [[nodiscard]] bool isFrameTraceEnabled() const noexcept;
    [[nodiscard]] bool isSessionTraceEnabled() const noexcept;

    [[nodiscard]] uint64_t nextTraceId() noexcept;
    [[nodiscard]] uint64_t nextSessionId() noexcept;

    [[nodiscard]] PerfConfig config() const noexcept;

private:
    PerfMonitor() = default;
    ~PerfMonitor();

    mutable std::shared_mutex configMutex_;
    PerfConfig config_;
    DeviceEnv deviceEnv_;

    mutable std::mutex errorMutex_;
    std::unordered_map<std::string, uint64_t> errors_;

    mutable std::mutex slowOpsMutex_;
    SlowOpsStats slowOpsStats_;

    mutable std::mutex resourceSamplerMutex_;
    std::unique_ptr<ResourceSampler> resourceSampler_;

    std::mutex reporterMutex_;
    std::vector<std::shared_ptr<IPerfReporter>> reporters_;

    std::atomic<uint64_t> traceIdCounter_{0};
    std::atomic<uint64_t> sessionIdCounter_{0};
};

} // namespace slark::perf
namespace slark::perf
