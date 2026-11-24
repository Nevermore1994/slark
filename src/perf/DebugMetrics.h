//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "ProcessMetrics.h"

namespace slark::perf {

struct DebugSample {
    int64_t timestampUs = 0;
    double cpuPercent = 0.0;
    int64_t rssKB = 0;
    int64_t vmSizeKB = 0;
};

inline constexpr size_t kDebugHistoryCapacity = 120;

class DebugMetrics {
 public:
    static DebugMetrics& instance() noexcept;

    void sample() noexcept;

    // Samples once, then returns the retained history as CSV lines:
    // timestampUs,cpuPercent,rssKB,vmSizeKB
    [[nodiscard]] std::string tick() noexcept;

    [[nodiscard]] DebugSample latest() const noexcept;
    [[nodiscard]] double latestCpuPercent() const noexcept;
    [[nodiscard]] size_t count() const noexcept;

    void reset() noexcept;

 private:
    DebugMetrics() = default;
    DebugMetrics(const DebugMetrics&) = delete;
    DebugMetrics& operator=(const DebugMetrics&) = delete;

    [[nodiscard]] std::string historyCsv() const noexcept;

    CpuBaseline cpuBaseline_;
    std::atomic_bool primed_{false};
    mutable std::mutex mutex_;
    std::array<DebugSample, kDebugHistoryCapacity> samples_{};
    size_t head_ = 0;
    size_t size_ = 0;
};

}  // namespace slark::perf
