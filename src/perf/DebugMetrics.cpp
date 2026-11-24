//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DebugMetrics.h"

#include <format>

#include "Time.hpp"

namespace slark::perf {

DebugMetrics& DebugMetrics::instance() noexcept {
    static DebugMetrics instance;
    return instance;
}

void DebugMetrics::sample() noexcept {
    DebugSample s;
    s.timestampUs = Time::systemNowUs();
    s.cpuPercent = sampleCpuPercent(cpuBaseline_);
    readProcessMemoryKB(s.rssKB, s.vmSizeKB);

    // The first CPU sample only primes the baseline and would otherwise plot a fake 0.
    if (!primed_.exchange(true, std::memory_order_relaxed)) {
        return;
    }

    std::lock_guard lock(mutex_);
    samples_[head_] = s;
    head_ = (head_ + 1) % kDebugHistoryCapacity;
    if (size_ < kDebugHistoryCapacity) {
        size_++;
    }
}

std::string DebugMetrics::tick() noexcept {
    sample();
    return historyCsv();
}

std::string DebugMetrics::historyCsv() const noexcept {
    std::lock_guard lock(mutex_);
    std::string out;
    const size_t start = size_ == kDebugHistoryCapacity ? head_ : 0;
    for (size_t i = 0; i < size_; ++i) {
        const auto& s = samples_[(start + i) % kDebugHistoryCapacity];
        out += std::format("{},{:.3f},{},{}\n", s.timestampUs, s.cpuPercent, s.rssKB, s.vmSizeKB);
    }
    return out;
}

DebugSample DebugMetrics::latest() const noexcept {
    std::lock_guard lock(mutex_);
    if (size_ == 0) {
        return {};
    }
    return samples_[(head_ + kDebugHistoryCapacity - 1) % kDebugHistoryCapacity];
}

double DebugMetrics::latestCpuPercent() const noexcept {
    return latest().cpuPercent;
}

size_t DebugMetrics::count() const noexcept {
    std::lock_guard lock(mutex_);
    return size_;
}

void DebugMetrics::reset() noexcept {
    {
        std::lock_guard lock(cpuBaseline_.mutex);
        cpuBaseline_.prevProc = 0;
        cpuBaseline_.prevTotal = 0;
    }
    primed_.store(false, std::memory_order_relaxed);
    std::lock_guard lock(mutex_);
    samples_.fill({});
    head_ = 0;
    size_ = 0;
}

}  // namespace slark::perf
