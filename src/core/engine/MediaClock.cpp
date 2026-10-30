//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "MediaClock.h"

namespace slark::engine {
namespace {

constexpr int64_t kAudioJitterUs = 10'000;

}  // namespace

Time::TimePoint MediaClock::nowLocked() const {
    if (!running_) {
        return anchorMedia_;
    }
    const auto elapsed = static_cast<double>((wall_.now() - anchorWall_).point());
    return Time::TimePoint(anchorMedia_.point() + static_cast<uint64_t>(elapsed * rate_));
}

void MediaClock::reanchorLocked(Time::TimePoint media) {
    anchorMedia_ = media;
    anchorWall_ = wall_.now();
}

void MediaClock::start() {
    std::lock_guard lock(mutex_);
    if (!running_) {
        reanchorLocked(anchorMedia_);
        running_ = true;
        allowBackwardAnchor_ = true;
    }
}

void MediaClock::stop() {
    std::lock_guard lock(mutex_);
    if (running_) {
        reanchorLocked(nowLocked());
        running_ = false;
    }
}

void MediaClock::seekTo(Time::TimePoint media) {
    std::lock_guard lock(mutex_);
    reanchorLocked(media);
}

void MediaClock::setRate(double rate) {
    std::lock_guard lock(mutex_);
    reanchorLocked(nowLocked());
    rate_ = rate;
}

void MediaClock::anchor(Time::TimePoint media) {
    std::lock_guard lock(mutex_);
    const auto behind = (nowLocked() - media).point();
    if (behind > 0 && (behind < kAudioJitterUs || !allowBackwardAnchor_)) {
        // Steady playback only corrects forward: going back makes the position jump.
        return;
    }
    allowBackwardAnchor_ = false;
    reanchorLocked(media);
}

void MediaClock::reset() {
    std::lock_guard lock(mutex_);
    anchorMedia_ = {};
    anchorWall_ = {};
    rate_ = 1.0;
    running_ = false;
    allowBackwardAnchor_ = false;
}

Time::TimePoint MediaClock::now() const {
    std::lock_guard lock(mutex_);
    return nowLocked();
}

bool MediaClock::running() const {
    std::lock_guard lock(mutex_);
    return running_;
}

double MediaClock::rate() const {
    std::lock_guard lock(mutex_);
    return rate_;
}

}  // namespace slark::engine
