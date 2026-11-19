//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <cstdint>
#include <mutex>

namespace slark::perf {

void readProcessMemoryKB(int64_t& rssKB, int64_t& vmSizeKB) noexcept;

struct CpuBaseline {
    std::mutex mutex;
    int64_t prevProc = 0;
    int64_t prevTotal = 0;
};

// Reads the platform counters and returns the CPU percentage since the previous call
// on this baseline. The first sample returns 0.
double sampleCpuPercent(CpuBaseline& baseline) noexcept;

double readProcessCpuPercent() noexcept;

}  // namespace slark::perf
