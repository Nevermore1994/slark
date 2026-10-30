//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

#include "Scheduler.h"

namespace slark::test {

// Deterministic scheduler driven by the test with virtual time. Nothing runs until
// runUntilIdle() or advance() is called, and there is no sleeping.
class ManualScheduler final : public Scheduler {
 public:
    // TimePoint{0} means "invalid" elsewhere, so virtual time starts above it.
    explicit ManualScheduler(uint64_t startUs = 1'000'000)
        : nowUs_(startUs), owner_(std::this_thread::get_id()) {}

    void execute(Task task) noexcept override {
        std::lock_guard lock(mutex_);
        immediate_.push_back(std::move(task));
    }

    void postAt(Slot slot, Time::TimePoint at, Task task) override {
        std::lock_guard lock(mutex_);
        eraseSlotLocked(slot);
        const Key key{at.point(), seq_++};
        timed_.emplace(key, Entry{slot, std::move(task)});
        slotKeys_[slot] = key;
    }

    void cancel(Slot slot) override {
        std::lock_guard lock(mutex_);
        eraseSlotLocked(slot);
    }

    [[nodiscard]] Time::TimePoint now() const noexcept override {
        std::lock_guard lock(mutex_);
        return Time::TimePoint(nowUs_);
    }

    [[nodiscard]] bool onSchedulerThread() const noexcept override {
        return std::this_thread::get_id() == owner_;
    }

    void runUntilIdle() {
        while (auto task = popReady()) {
            (*task)();
        }
    }

    // Stops at every timer on the way so each task observes the time it was scheduled for.
    void advance(std::chrono::microseconds d) {
        const uint64_t target = now().point() + static_cast<uint64_t>(d.count());
        for (;;) {
            runUntilIdle();
            std::lock_guard lock(mutex_);
            if (timed_.empty() || timed_.begin()->first.first > target) {
                nowUs_ = std::max(nowUs_, target);
                return;
            }
            nowUs_ = std::max(nowUs_, timed_.begin()->first.first);
        }
    }

    [[nodiscard]] size_t pendingTimers() const {
        std::lock_guard lock(mutex_);
        return timed_.size();
    }

 private:
    using Key = std::pair<uint64_t, uint64_t>;  // (fire time, sequence)

    struct Entry {
        Slot slot;
        Task task;
    };

    void eraseSlotLocked(Slot slot) {
        if (auto it = slotKeys_.find(slot); it != slotKeys_.end()) {
            timed_.erase(it->second);
            slotKeys_.erase(it);
        }
    }

    std::optional<Task> popReady() {
        std::lock_guard lock(mutex_);
        if (!immediate_.empty()) {
            auto task = std::move(immediate_.front());
            immediate_.pop_front();
            return task;
        }
        if (timed_.empty() || timed_.begin()->first.first > nowUs_) {
            return std::nullopt;
        }
        auto it = timed_.begin();
        auto task = std::move(it->second.task);
        slotKeys_.erase(it->second.slot);
        timed_.erase(it);
        return task;
    }

    mutable std::mutex mutex_;
    uint64_t nowUs_;
    const std::thread::id owner_;
    std::deque<Task> immediate_;
    std::map<Key, Entry> timed_;
    std::unordered_map<Slot, Key> slotKeys_;
    uint64_t seq_ = 0;
};

}  // namespace slark::test
