//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PerfMonitor.h"

#include <algorithm>

#include "Log.hpp"
#include "PlaySession.h"

namespace slark::perf {

// PlaySession::finish 通过此函数回调 PerfMonitor，避免头文件循环依赖
void perfMonitorReportSession(PlaySession& session) {
    PerfMonitor::instance().reportSession(session);
}

PerfMonitor& PerfMonitor::instance() {
    static PerfMonitor inst;
    return inst;
}

PerfMonitor::~PerfMonitor() {
    shutdown();
}

void PerfMonitor::configure(const PerfConfig& config) {
    // 软重配：只停止采样器和重置统计，不清空 reporters_
    {
        std::lock_guard<std::mutex> lock(resourceSamplerMutex_);
        if (resourceSampler_) {
            resourceSampler_->stop();
            resourceSampler_.reset();
        }
    }

    {
        std::unique_lock<std::shared_mutex> wlock(configMutex_);
        config_ = config;
    }

    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        errors_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(slowOpsMutex_);
        slowOpsStats_ = {};
    }

    if (!config.enabled) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(slowOpsMutex_);
        slowOpsStats_.thresholdMs = config.slowThresholdMs;
    }

    if (config.enableResourceSampling && config.resourceIntervalMs > 0) {
        std::lock_guard<std::mutex> lock(resourceSamplerMutex_);
        resourceSampler_ = std::make_unique<ResourceSampler>(config.resourceIntervalMs);
        resourceSampler_->start();
    }

    LogI("[Perf] configured: enabled={} frameTrace={} sessionTrace={} resourceSampling={} "
         "slowThreshold={}ms resourceInterval={}ms",
         config.enabled,
         config.enableFrameTrace,
         config.enableSessionTrace,
         config.enableResourceSampling,
         config.slowThresholdMs,
         config.resourceIntervalMs);
}

void PerfMonitor::setDeviceEnv(const DeviceEnv& env) {
    std::unique_lock<std::shared_mutex> wlock(configMutex_);
    deviceEnv_ = env;
}

void PerfMonitor::shutdown() {
    {
        std::lock_guard<std::mutex> lock(resourceSamplerMutex_);
        if (resourceSampler_) {
            resourceSampler_->stop();
            resourceSampler_.reset();
        }
    }

    {
        std::unique_lock<std::shared_mutex> wlock(configMutex_);
        config_.enabled = false;
    }

    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        errors_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(slowOpsMutex_);
        slowOpsStats_ = {};
    }
    {
        std::lock_guard<std::mutex> lock(reporterMutex_);
        reporters_.clear();
    }

    traceIdCounter_.store(0, std::memory_order_relaxed);
    sessionIdCounter_.store(0, std::memory_order_relaxed);
}

void PerfMonitor::addReporter(std::shared_ptr<IPerfReporter> reporter) {
    if (!reporter) {
        return;
    }
    std::lock_guard<std::mutex> lock(reporterMutex_);
    reporters_.push_back(std::move(reporter));
}

void PerfMonitor::removeReporter(const std::shared_ptr<IPerfReporter>& reporter) {
    std::lock_guard<std::mutex> lock(reporterMutex_);
    reporters_.erase(std::remove(reporters_.begin(), reporters_.end(), reporter), reporters_.end());
}

void PerfMonitor::registerSession(std::shared_ptr<PlaySession> session) {
    std::lock_guard<std::mutex> lock(resourceSamplerMutex_);
    if (resourceSampler_ && session) {
        resourceSampler_->registerSession(session);
    }
}

void PerfMonitor::unregisterSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(resourceSamplerMutex_);
    if (resourceSampler_) {
        resourceSampler_->unregisterSession(sessionId);
    }
}

void PerfMonitor::reportFrame(std::shared_ptr<const FrameTrace> trace) {
    if (!isFrameTraceEnabled() || !trace) {
        return;
    }

    std::vector<std::shared_ptr<IPerfReporter>> snapshot;
    {
        std::lock_guard<std::mutex> lock(reporterMutex_);
        snapshot = reporters_;
    }
    for (size_t i = 0; i < snapshot.size(); ++i) {
        try {
            if (i + 1 == snapshot.size()) {
                snapshot[i]->onFrameTrace(std::move(trace));
            } else {
                snapshot[i]->onFrameTrace(trace);
            }
        } catch (const std::exception& e) {
            LogE("[Perf] onFrameTrace reporter[{}] threw: {}", i, e.what());
        } catch (...) {
            LogE("[Perf] onFrameTrace reporter[{}] threw unknown exception", i);
        }
    }
}

void PerfMonitor::reportSession(PlaySession& session) {
    if (!isEnabled()) {
        return;
    }

    if (!isSessionTraceEnabled()) {
        return;
    }

    auto report = std::make_shared<SessionReport>();
    report->sessionId = session.sessionId();
    report->url = session.url();
    report->elapsedMs = session.elapsedMs();
    report->firstFrameMs = session.firstFrameMs();
    report->success = session.isSuccess();
    report->milestones = session.milestonesSnapshot();
    report->stageStats = session.stageStatsSnapshot();
    report->frameStats = session.frameStatsSnapshot();
    report->resourceStats = session.resourceStatsSnapshot();
    {
        std::shared_lock<std::shared_mutex> rlock(configMutex_);
        report->deviceEnv = deviceEnv_;
    }

    std::vector<std::shared_ptr<IPerfReporter>> snapshot;
    {
        std::lock_guard<std::mutex> lock(reporterMutex_);
        snapshot = reporters_;
    }
    // report 为 const，所有 reporter 共享同一份只读数据
    const auto constReport = std::shared_ptr<const SessionReport>(std::move(report));
    for (size_t i = 0; i < snapshot.size(); ++i) {
        try {
            snapshot[i]->onSessionComplete(constReport);
        } catch (const std::exception& e) {
            LogE("[Perf] onSessionComplete reporter[{}] threw: {}", i, e.what());
        } catch (...) {
            LogE("[Perf] onSessionComplete reporter[{}] threw unknown exception", i);
        }
    }
}

void PerfMonitor::recordSlowOp(const std::string& stage, double durationMs) {
    if (!isEnabled()) {
        return;
    }
    LogD("[Perf] slow op: {} took {:.1f}ms", stage, durationMs);
    std::lock_guard<std::mutex> lock(slowOpsMutex_);
    slowOpsStats_.record(stage, durationMs);
}

void PerfMonitor::recordError(const std::string& reason) {
    if (!isEnabled()) {
        return;
    }
    LogE("[Perf] error: {}", reason);
    std::lock_guard<std::mutex> lock(errorMutex_);
    ++errors_[reason];
}

SlowOpsStats PerfMonitor::slowOpsSnapshot() const {
    std::lock_guard<std::mutex> lock(slowOpsMutex_);
    return slowOpsStats_;
}

std::unordered_map<std::string, uint64_t> PerfMonitor::errorsSnapshot() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return errors_;
}

bool PerfMonitor::isEnabled() const noexcept {
    std::shared_lock<std::shared_mutex> rlock(configMutex_);
    return config_.enabled;
}

bool PerfMonitor::isFrameTraceEnabled() const noexcept {
    std::shared_lock<std::shared_mutex> rlock(configMutex_);
    return config_.enabled && config_.enableFrameTrace;
}

bool PerfMonitor::isSessionTraceEnabled() const noexcept {
    std::shared_lock<std::shared_mutex> rlock(configMutex_);
    return config_.enabled && config_.enableSessionTrace;
}

uint64_t PerfMonitor::nextTraceId() noexcept {
    return traceIdCounter_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t PerfMonitor::nextSessionId() noexcept {
    return sessionIdCounter_.fetch_add(1, std::memory_order_relaxed);
}

PerfConfig PerfMonitor::config() const noexcept {
    std::shared_lock<std::shared_mutex> rlock(configMutex_);
    return config_;
}

}  // namespace slark::perf
