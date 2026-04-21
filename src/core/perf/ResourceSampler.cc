//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "ResourceSampler.h"

#include "PlaySession.h"
#include "Time.hpp"

#include <algorithm>

#if defined(__ANDROID__)
#include <fstream>
#include <sstream>
#elif defined(__APPLE__) || defined(__MACH__)
#include <mach/mach.h>
#include <mach/task_info.h>
#endif

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
    activeSessions_.erase(std::remove_if(activeSessions_.begin(),
                                         activeSessions_.end(),
                                         [&sessionId](auto& weak) {
                                             auto s = weak.lock();
                                             return !s || s->sessionId() == sessionId;
                                         }),
                          activeSessions_.end());
}

void ResourceSampler::sampleLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        auto snap = sampleOnce();

        先拷贝快照，释放锁后再回调，避免持锁期间执行重操作
        std::vector<std::shared_ptr<PlaySession>> liveSessions;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            activeSessions_.erase(std::remove_if(activeSessions_.begin(),
                                                 activeSessions_.end(),
                                                 [&liveSessions](auto& weak) {
                                                     if (auto s = weak.lock()) {
                                                         liveSessions.push_back(s);
                                                         return false;
                                                     }
                                                     return true;
                                                 }),
                                  activeSessions_.end());
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
    snap.timestampUs = static_cast<int64_t>(Time::nowTimeStamp().point());
    snap.cpuPercent = calcCpuUsage();
    readMemoryInfo(snap.rssKB, snap.vmSizeKB);
    return snap;
}

#if defined(__ANDROID__)

double ResourceSampler::calcCpuUsage() {
    std::ifstream procStat("/proc/self/stat");
    if (!procStat.is_open()) {
        return 0.0;
    }
    std::string line;
    std::getline(procStat, line);

    auto commEnd = line.rfind(')');
    if (commEnd == std::string::npos || commEnd + 2 >= line.size()) {
        return 0.0;
    }

    std::istringstream iss(line.substr(commEnd + 2));
    std::string token;
    for (int32_t i = 0; i < 11; ++i) {
        if (!(iss >> token)) {
            return 0.0;
        }
    }

    int64_t utime = 0, stime = 0;
    if (!(iss >> utime >> stime)) {
        return 0.0;
    }
    int64_t procTicks = utime + stime;

    std::ifstream sysStat("/proc/stat");
    if (!sysStat.is_open()) {
        return 0.0;
    }
    std::string cpuLine;
    std::getline(sysStat, cpuLine);

    std::istringstream cpuIss(cpuLine);
    std::string cpuLabel;
    cpuIss >> cpuLabel;
    if (cpuLabel != "cpu") {
        return 0.0;
    }

    int64_t sysTicks = 0, val = 0;
    while (cpuIss >> val) {
        sysTicks += val;
    }

    double usage = 0.0;
    if (prevSysTicks_ > 0 && sysTicks > prevSysTicks_) {
        int64_t deltaSys = sysTicks - prevSysTicks_;
        int64_t deltaProc = procTicks - prevProcTicks_;
        单核归一化：进程 tick / 系统总 tick* 100，与 Apple 端口径一致 usage =
            static_cast<double>(deltaProc) / static_cast<double>(deltaSys) * 100.0;
    }
    prevProcTicks_ = procTicks;
    prevSysTicks_ = sysTicks;
    return usage;
}

void ResourceSampler::readMemoryInfo(int64_t& rssKB, int64_t& vmSizeKB) {
    rssKB = 0;
    vmSizeKB = 0;
    std::ifstream status("/proc/self/status");
    if (!status.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::istringstream iss(line.substr(6));
            iss >> rssKB;
        } else if (line.compare(0, 7, "VmSize:") == 0) {
            std::istringstream iss(line.substr(7));
            iss >> vmSizeKB;
        }
        if (rssKB > 0 && vmSizeKB > 0) {
            break;
        }
    }
}

#elif defined(__APPLE__) || defined(__MACH__)

double ResourceSampler::calcCpuUsage() {
    task_thread_times_info_data_t taskTimes;
    mach_msg_type_number_t count = TASK_THREAD_TIMES_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(),
                                 TASK_THREAD_TIMES_INFO,
                                 reinterpret_cast<task_info_t>(&taskTimes),
                                 &count);
    if (kr != KERN_SUCCESS) {
        return 0.0;
    }

    int64_t procTimeUs = static_cast<int64_t>(taskTimes.user_time.seconds) * 1000000 +
                         static_cast<int64_t>(taskTimes.user_time.microseconds) +
                         static_cast<int64_t>(taskTimes.system_time.seconds) * 1000000 +
                         static_cast<int64_t>(taskTimes.system_time.microseconds);
    int64_t wallTimeUs = static_cast<int64_t>(Time::nowTimeStamp().point());

    double usage = 0.0;
    if (prevWallTimeUs_ > 0 && wallTimeUs > prevWallTimeUs_) {
        int64_t deltaWall = wallTimeUs - prevWallTimeUs_;
        int64_t deltaProc = procTimeUs - prevProcTimeUs_;
        usage = static_cast<double>(deltaProc) / static_cast<double>(deltaWall) * 100.0;
    }
    prevProcTimeUs_ = procTimeUs;
    prevWallTimeUs_ = wallTimeUs;
    return usage;
}

void ResourceSampler::readMemoryInfo(int64_t& rssKB, int64_t& vmSizeKB) {
    rssKB = 0;
    vmSizeKB = 0;
    mach_task_basic_info_data_t taskInfo;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(),
                                 MACH_TASK_BASIC_INFO,
                                 reinterpret_cast<task_info_t>(&taskInfo),
                                 &count);
    if (kr != KERN_SUCCESS) {
        return;
    }
    rssKB = static_cast<int64_t>(taskInfo.resident_size) / 1024;
    vmSizeKB = static_cast<int64_t>(taskInfo.virtual_size) / 1024;
}

#else

double ResourceSampler::calcCpuUsage() {
    return 0.0;
}

void ResourceSampler::readMemoryInfo(int64_t& rssKB, int64_t& vmSizeKB) {
    rssKB = 0;
    vmSizeKB = 0;
}

#endif

} // namespace slark::perf
namespace slark::perf
