//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "TimerPool.h"

#include <algorithm>
#include <ranges>

namespace slark {

using std::chrono::milliseconds;
using namespace std::chrono_literals;

TimerPool::~TimerPool() {
    clear();
}

uint64_t TimerPool::nowMs() const noexcept {
    return static_cast<uint64_t>(Time::nowTimeStamp().toMilliSeconds().count());
}

void TimerPool::clear() noexcept {
    std::ranges::for_each(level1_, [](auto& s) {
        s.clear();
    });
    std::ranges::for_each(level2_, [](auto& s) {
        s.clear();
    });
    overflow_.clear();
    timers_.clear();
    currentTick_ = 0;
    startMs_ = 0;
    earliestExpireMs_ = UINT64_MAX;
}

bool TimerPool::empty() const noexcept {
    return timers_.empty();
}

milliseconds TimerPool::peekWaitTime() const noexcept {
    if (timers_.empty() || earliestExpireMs_ == UINT64_MAX) {
        return 1000ms;
    }
    auto now = nowMs();
    return (earliestExpireMs_ <= now) ? 0ms : milliseconds(earliestExpireMs_ - now);
}

void TimerPool::placeInWheel(const WheelEntry& entry) noexcept {
    auto base = startMs_ + currentTick_;
    auto diff = (entry.expireMs > base) ? (entry.expireMs - base) : 0ULL;

    if (diff < kLevel1RangeMs) {
        level1_[(currentTick_ + diff) % kWheelSize].push_back(entry);
    } else if (diff < kLevel2RangeMs) {
        level2_[((currentTick_ + diff) / kLevel2TickMs) % kWheelSize].push_back(entry);
    } else {
        overflow_.push_back(entry);
    }
}

void TimerPool::cascadeLevel2(uint32_t slot) noexcept {
    for (auto& e : level2_[slot]) {
        if (auto it = timers_.find(e.timerId); it != timers_.end() && it->second.isValid) {
            placeInWheel(e);
        }
    }
    level2_[slot].clear();
}

void TimerPool::drainSlot(Slot& slot, std::vector<TimerEntry>& expired) noexcept {
    for (auto& we : slot) {
        if (auto it = timers_.find(we.timerId); it != timers_.end() && it->second.isValid) {
            expired.push_back(std::move(it->second));
            timers_.erase(it);
        }
    }
    slot.clear();
}

void TimerPool::redistributeOverflow() noexcept {
    std::list<WheelEntry> temp;
    temp.swap(overflow_);
    for (auto& e : temp) {
        if (auto it = timers_.find(e.timerId); it != timers_.end() && it->second.isValid) {
            placeInWheel(e);
        }
    }
}

void TimerPool::collectExpired(std::vector<TimerEntry>& expired) noexcept {
    auto now = nowMs();
    if (startMs_ == 0) {
        startMs_ = now;
    }

    auto targetTick = now - startMs_;
    // Limit ticks advanced per call to avoid jank on large time jumps
    constexpr uint64_t kMaxTicksPerCall = 1024;
    auto maxTick = currentTick_ + kMaxTicksPerCall;
    if (targetTick > maxTick) {
        targetTick = maxTick;
    }

    // Advance tick by tick, handling all slots in [currentTick_, targetTick)
    while (currentTick_ < targetTick) {
        auto idx = static_cast<uint32_t>(currentTick_ % kWheelSize);

        if (idx == 0 && currentTick_ > 0) {
            auto l2 = static_cast<uint32_t>((currentTick_ / kLevel2TickMs) % kWheelSize);
            cascadeLevel2(l2);
            if (l2 == 0) {
                redistributeOverflow();
            }
        }

        drainSlot(level1_[idx], expired);
        ++currentTick_;
    }
    auto idx = static_cast<uint32_t>(currentTick_ % kWheelSize);
    if (idx == 0 && currentTick_ > 0) {
        auto l2 = static_cast<uint32_t>((currentTick_ / kLevel2TickMs) % kWheelSize);
        cascadeLevel2(l2);
        if (l2 == 0) {
            redistributeOverflow();
        }
    }
    drainSlot(level1_[idx], expired);
    // Expired timers were removed, refresh the cache
    refreshEarliestExpire();
}

void TimerPool::refreshEarliestExpire() noexcept {
    earliestExpireMs_ = UINT64_MAX;
    for (const auto& [_, entry] : timers_) {
        if (entry.isValid) {
            earliestExpireMs_ =
                std::min(earliestExpireMs_,
                         static_cast<uint64_t>(entry.expireTime.toMilliSeconds().count()));
        }
    }
}

TimerId TimerPool::addTimer(TimerEntry entry) noexcept {
    if (startMs_ == 0) {
        startMs_ = nowMs();
    }
    auto id = entry.id;
    auto expMs = static_cast<uint64_t>(entry.expireTime.toMilliSeconds().count());
    earliestExpireMs_ = std::min(earliestExpireMs_, expMs);
    timers_.emplace(id, std::move(entry));
    placeInWheel({id, expMs});
    return id;
}

void TimerPool::reinsert(TimerEntry entry) noexcept {
    auto id = entry.id;
    auto expMs = static_cast<uint64_t>(entry.expireTime.toMilliSeconds().count());
    earliestExpireMs_ = std::min(earliestExpireMs_, expMs);
    // Explicitly overwrite to avoid silent failure
    timers_.insert_or_assign(id, std::move(entry));
    placeInWheel({id, expMs});
}

void TimerPool::cancel(TimerId id) noexcept {
    if (auto it = timers_.find(id); it != timers_.end()) {
        it->second.isValid = false;
        // The cancelled timer may be the earliest one, refresh if so
        auto expMs = static_cast<uint64_t>(it->second.expireTime.toMilliSeconds().count());
        if (expMs <= earliestExpireMs_) {
            refreshEarliestExpire();
        }
    }
}

}  // end namespace slark
