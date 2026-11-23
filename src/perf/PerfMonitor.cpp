//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PerfMonitor.h"

#include <algorithm>

#include "Log.hpp"
#include "NetworkPerf.h"
#include "PlaySession.h"

namespace slark::perf {

// PlaySession::finish calls back into PerfMonitor through this function to avoid
// circular header dependencies
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
    // The HTTP layer publishes request samples through a header-only bridge so
    // src/http never links against slark_perf. Disable the bridge when off.
    if (config.enabled && config.enableSessionTrace) {
        setNetworkSampleSink(
            [](const NetworkSample& sample) { PerfMonitor::instance().recordNetwork(sample); });
    } else {
        setNetworkSampleSink({});
    }

    // Soft reconfig: stop the sampler and reset stats only, keep reporters_
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
         config.enabled, config.enableFrameTrace, config.enableSessionTrace,
         config.enableResourceSampling, config.slowThresholdMs, config.resourceIntervalMs);
}

void PerfMonitor::setDeviceEnv(const DeviceEnv& env) {
    std::unique_lock<std::shared_mutex> wlock(configMutex_);
    deviceEnv_ = env;
}

void PerfMonitor::shutdown() {
    setNetworkSampleSink({});
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
        std::lock_guard<std::mutex> lock(networkMutex_);
        sessions_.clear();
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
    if (!session) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(networkMutex_);
        sessions_[session->sessionId()] = session;
    }
    std::lock_guard<std::mutex> lock(resourceSamplerMutex_);
    if (resourceSampler_) {
        resourceSampler_->registerSession(std::move(session));
    }
}

void PerfMonitor::unregisterSession(const std::string& sessionId) {
    {
        std::lock_guard<std::mutex> lock(networkMutex_);
        if (sessions_.erase(sessionId) == 0) {
            return;
        }
    }
    std::lock_guard<std::mutex> lock(resourceSamplerMutex_);
    if (resourceSampler_) {
        resourceSampler_->unregisterSession(sessionId);
    }
}

void PerfMonitor::recordNetwork(const NetworkSample& sample) {
    if (!isEnabled()) {
        return;
    }
    std::shared_ptr<PlaySession> session;
    {
        std::lock_guard<std::mutex> lock(networkMutex_);
        const auto it = sessions_.find(sample.perfSessionId);
        if (it == sessions_.end()) {
            return;
        }
        session = it->second;
    }
    session->recordNetwork(sample);
}

NetworkStats PerfMonitor::networkSnapshot(const std::string& sessionId) const {
    std::shared_ptr<PlaySession> session;
    {
        std::lock_guard<std::mutex> lock(networkMutex_);
        const auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            return {};
        }
        session = it->second;
    }
    return session->networkStatsSnapshot();
}

size_t PerfMonitor::activeSessionCount() const {
    std::lock_guard<std::mutex> lock(networkMutex_);
    return sessions_.size();
}

std::vector<std::string> PerfMonitor::activeSessionIds() const {
    std::lock_guard<std::mutex> lock(networkMutex_);
    std::vector<std::string> ids;
    ids.reserve(sessions_.size());
    for (const auto& [id, session] : sessions_) {
        (void)session;
        ids.push_back(id);
    }
    return ids;
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
    report->networkStats = session.networkStatsSnapshot();
    {
        std::shared_lock<std::shared_mutex> rlock(configMutex_);
        report->deviceEnv = deviceEnv_;
    }

    const auto& network = report->networkStats;
    LogI("[Perf] session {}: {:.1f}ms firstFrame={:.1f}ms requests={} ok={} failed={} "
         "networkMs={:.1f} avg={:.1f}ms bytes={} success={}",
         report->sessionId, report->elapsedMs, report->firstFrameMs, network.requestCount,
         network.successCount, network.failureCount,
         static_cast<double>(network.totalRequestMs), network.avgRequestMs(),
         network.totalBodyBytes, report->success);

    std::vector<std::shared_ptr<IPerfReporter>> snapshot;
    {
        std::lock_guard<std::mutex> lock(reporterMutex_);
        snapshot = reporters_;
    }
    // report is const; all reporters share the same read-only data
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
