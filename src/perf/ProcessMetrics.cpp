//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "ProcessMetrics.h"

#include "Time.hpp"

#if defined(__ANDROID__)
#include <fstream>
#include <sstream>
#include <string>
#elif defined(__APPLE__) || defined(__MACH__)
#include <mach/mach.h>
#include <mach/task_info.h>
#endif

namespace slark::perf {
namespace {

#if defined(__ANDROID__)

void readMemory(int64_t& rssKB, int64_t& vmSizeKB) noexcept {
    std::ifstream status("/proc/self/status");
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

bool readCounters(int64_t& proc, int64_t& total) noexcept {
    std::ifstream procStat("/proc/self/stat");
    std::string line;
    if (!procStat.is_open() || !std::getline(procStat, line)) {
        return false;
    }
    const auto commEnd = line.rfind(')');
    if (commEnd == std::string::npos || commEnd + 2 >= line.size()) {
        return false;
    }
    std::istringstream iss(line.substr(commEnd + 2));
    std::string token;
    for (int32_t i = 0; i < 11; ++i) {
        if (!(iss >> token)) {
            return false;
        }
    }
    int64_t utime = 0;
    int64_t stime = 0;
    if (!(iss >> utime >> stime)) {
        return false;
    }
    proc = utime + stime;

    std::ifstream sysStat("/proc/stat");
    std::string cpuLine;
    if (!sysStat.is_open() || !std::getline(sysStat, cpuLine)) {
        return false;
    }
    std::istringstream cpuIss(cpuLine);
    std::string label;
    cpuIss >> label;
    if (label != "cpu") {
        return false;
    }
    int64_t value = 0;
    while (cpuIss >> value) {
        total += value;
    }
    return true;
}

#elif defined(__APPLE__) || defined(__MACH__)

void readMemory(int64_t& rssKB, int64_t& vmSizeKB) noexcept {
    mach_task_basic_info_data_t taskInfo;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&taskInfo), &count) != KERN_SUCCESS) {
        return;
    }
    rssKB = static_cast<int64_t>(taskInfo.resident_size) / 1024;
    vmSizeKB = static_cast<int64_t>(taskInfo.virtual_size) / 1024;
}

bool readCounters(int64_t& proc, int64_t& total) noexcept {
    task_thread_times_info_data_t taskTimes;
    mach_msg_type_number_t count = TASK_THREAD_TIMES_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_THREAD_TIMES_INFO,
                  reinterpret_cast<task_info_t>(&taskTimes), &count) != KERN_SUCCESS) {
        return false;
    }
    proc = static_cast<int64_t>(taskTimes.user_time.seconds) * 1000000 +
           static_cast<int64_t>(taskTimes.user_time.microseconds) +
           static_cast<int64_t>(taskTimes.system_time.seconds) * 1000000 +
           static_cast<int64_t>(taskTimes.system_time.microseconds);
    total = static_cast<int64_t>(Time::nowTimeStamp().point());
    return true;
}

#else

void readMemory(int64_t&, int64_t&) noexcept {}
bool readCounters(int64_t&, int64_t&) noexcept {
    return false;
}

#endif

}  // namespace

void readProcessMemoryKB(int64_t& rssKB, int64_t& vmSizeKB) noexcept {
    rssKB = 0;
    vmSizeKB = 0;
    readMemory(rssKB, vmSizeKB);
}

double sampleCpuPercent(CpuBaseline& baseline) noexcept {
    int64_t proc = 0;
    int64_t total = 0;
    if (!readCounters(proc, total)) {
        return 0.0;
    }
    std::lock_guard<std::mutex> lock(baseline.mutex);
    double usage = 0.0;
    if (baseline.prevTotal > 0 && total > baseline.prevTotal) {
        usage = static_cast<double>(proc - baseline.prevProc) /
                static_cast<double>(total - baseline.prevTotal) * 100.0;
    }
    baseline.prevProc = proc;
    baseline.prevTotal = total;
    return usage;
}

double readProcessCpuPercent() noexcept {
    static CpuBaseline baseline;
    return sampleCpuPercent(baseline);
}

}  // namespace slark::perf
