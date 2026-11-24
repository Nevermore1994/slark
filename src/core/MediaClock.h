//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <mutex>

#include "Scheduler.h"
#include "Time.hpp"

namespace slark::engine {

class MediaClock {
 public:
    explicit MediaClock(const Scheduler& wall) noexcept : wall_(wall) {}

    void start();
    void stop();
    void seekTo(Time::TimePoint media);
    void setRate(double rate);
    void anchor(Time::TimePoint media);
    void reset();

    [[nodiscard]] Time::TimePoint now() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] double rate() const;

 private:
    [[nodiscard]] Time::TimePoint nowLocked() const;
    void reanchorLocked(Time::TimePoint media);

    const Scheduler& wall_;
    mutable std::mutex mutex_;
    Time::TimePoint anchorMedia_{};
    Time::TimePoint anchorWall_{};
    double rate_ = 1.0;
    bool running_ = false;
    bool allowBackwardAnchor_ = false;
};

}  // namespace slark::engine
