//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NonCopyable.h"
#include "Timer.h"

#include <array>
#include <chrono>
#include <list>
#include <unordered_map>
#include <vector>

namespace slark {

/// 纯时间轮数据结构
class TimerPool : public NonCopyMovable {
public:
    TimerPool() = default;
    ~TimerPool();

    TimerId addTimer(TimerEntry entry) noexcept;
    void reinsert(TimerEntry entry) noexcept;
    void cancel(TimerId id) noexcept;
    void collectExpired(std::vector<TimerEntry>& expired) noexcept;
    [[nodiscard]] std::chrono::milliseconds peekWaitTime() const noexcept;
    void clear() noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    static constexpr uint32_t kWheelSize = 256;
    static constexpr uint32_t kLevel1RangeMs = kWheelSize;                 // 256ms
    static constexpr uint32_t kLevel2TickMs = kLevel1RangeMs;              // 256ms
    static constexpr uint32_t kLevel2RangeMs = kWheelSize * kLevel2TickMs; // 65536ms

    struct WheelEntry {
        uint64_t timerId;
        uint64_t expireMs;
    };

    using Slot = std::list<WheelEntry>;

    [[nodiscard]] uint64_t nowMs() const noexcept;
    void placeInWheel(const WheelEntry& entry) noexcept;
    void cascadeLevel2(uint32_t slot) noexcept;
    void drainSlot(Slot& slot, std::vector<TimerEntry>& expired) noexcept;
    void redistributeOverflow() noexcept;
    void refreshEarliestExpire() noexcept;

private:
    uint64_t currentTick_ = 0;
    uint64_t startMs_ = 0;
    uint64_t earliestExpireMs_ = UINT64_MAX;

    std::array<Slot, kWheelSize> level1_{};
    std::array<Slot, kWheelSize> level2_{};
    std::list<WheelEntry> overflow_;
    std::unordered_map<uint64_t, TimerEntry> timers_;
};

} // namespace slark
