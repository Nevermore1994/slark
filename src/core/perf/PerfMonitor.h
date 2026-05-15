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

    void configure(const PerfConfig& config);

    void setDeviceEnv(const DeviceEnv& env);

    void shutdown();

    void addReporter(std::shared_ptr<IPerfReporter> reporter);
    void removeReporter(const std::shared_ptr<IPerfReporter>& reporter);

    void registerSession(std::shared_ptr<PlaySession> session);
    void unregisterSession(const std::string& sessionId);

    void reportFrame(std::shared_ptr<const FrameTrace> trace);

    void reportSession(PlaySession& session);

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
