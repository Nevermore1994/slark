//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include "MoveOnlyFunction.hpp"
#include "Time.hpp"

namespace slark {

using TimerTask = MoveOnlyFunction<void()>;

struct TimerId {
 private:
    uint64_t id_ = kInvalidTimerId;

 public:
    static constexpr uint64_t kInvalidTimerId = 0;

    TimerId() = default;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "google-explicit-constructor"

    TimerId(uint64_t t) : id_(t) {}

    operator uint64_t() const {
        return id_;
    }

#pragma clang diagnostic pop

    [[nodiscard]] bool isValid() const noexcept {
        return id_ != kInvalidTimerId;
    }

    uint64_t id() const noexcept {
        return id_;
    }

    [[nodiscard]] bool operator==(const TimerId& other) const noexcept {
        return id_ == other.id_;
    }

    static TimerId next() noexcept {
        static std::atomic<uint64_t> counter{1};
        return TimerId{counter.fetch_add(1, std::memory_order_relaxed)};
    }
};

struct TimerEntry {
    TimerId id;
    Time::TimePoint expireTime;
    TimerTask task;
    bool isLoop = false;
    std::chrono::milliseconds loopInterval{};
    bool isValid = true;
};

}  // namespace slark

template <>
struct std::hash<slark::TimerId> {
    size_t operator()(const slark::TimerId& id) const noexcept {
        return std::hash<uint64_t>{}(id.id());
    }
};
