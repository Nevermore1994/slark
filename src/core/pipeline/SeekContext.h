//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "NonCopyable.h"
#include "Time.hpp"

namespace slark {

enum class SeekStrategy : uint8_t {
    FastForwardInGOP,
    SeekToKeyframe,
};

class SeekContext : public NonCopyMovable {
 public:
    SeekContext() = default;
    ~SeekContext() = default;

    [[nodiscard]] bool beginSeek(CTime target, SeekStrategy strategy) noexcept {
        bool superseded = isSeekPending_.load(std::memory_order_acquire);
        seekEpoch_.fetch_add(1, std::memory_order_acq_rel);
        activeSeekTimeSec_.store(target.second(), std::memory_order_release);
        strategy_.store(static_cast<uint8_t>(strategy), std::memory_order_release);
        isSeekPending_.store(true, std::memory_order_release);
        isVideoSeeked_.store(false, std::memory_order_release);
        isAudioSeeked_.store(false, std::memory_order_release);
        return superseded;
    }

    void updateGOPRange(double keyframePts, double gopEndPts) noexcept {
        gopKeyframePts_.store(keyframePts, std::memory_order_release);
        gopEndPts_.store(gopEndPts, std::memory_order_release);
    }

    void updateTarget(double sec) noexcept {
        activeSeekTimeSec_.store(sec, std::memory_order_release);
    }

    [[nodiscard]] SeekStrategy strategy() const noexcept {
        return static_cast<SeekStrategy>(strategy_.load(std::memory_order_acquire));
    }

    [[nodiscard]] double gopKeyframePts() const noexcept {
        return gopKeyframePts_.load(std::memory_order_acquire);
    }

    [[nodiscard]] double gopEndPts() const noexcept {
        return gopEndPts_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool completeSeek() noexcept {
        bool expected = true;
        if (!isSeekPending_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            return false;
        }
        activeSeekTimeSec_.store(kNoSeekTime, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool isSeekPending() const noexcept {
        return isSeekPending_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<double> targetSeekTime() const noexcept {
        double t = activeSeekTimeSec_.load(std::memory_order_acquire);
        if (t < 0.0) {
            return std::nullopt;
        }
        return t;
    }

    [[nodiscard]] uint64_t currentEpoch() const noexcept {
        return seekEpoch_.load(std::memory_order_acquire);
    }

    void markVideoSeeked() noexcept {
        isVideoSeeked_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool tryMarkVideoSeeked() noexcept {
        bool expected = false;
        return isVideoSeeked_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    }

    void markAudioSeeked() noexcept {
        isAudioSeeked_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool isVideoSeeked() const noexcept {
        return isVideoSeeked_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isAudioSeeked() const noexcept {
        return isAudioSeeked_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isAllTracksSeeked(bool hasVideo, bool hasAudio) const noexcept {
        return (!hasVideo || isVideoSeeked_.load(std::memory_order_acquire)) &&
               (!hasAudio || isAudioSeeked_.load(std::memory_order_acquire));
    }

    [[nodiscard]] bool isEpochStale(uint64_t capturedEpoch) const noexcept {
        return capturedEpoch != seekEpoch_.load(std::memory_order_acquire);
    }

    void setMaxSkipFrames(int32_t n) noexcept {
        maxSkipFrames_.store(n, std::memory_order_release);
    }

    [[nodiscard]] int32_t maxSkipFrames() const noexcept {
        return maxSkipFrames_.load(std::memory_order_acquire);
    }

 private:
    static constexpr double kNoSeekTime = -1.0;

    std::atomic<uint64_t> seekEpoch_{0};
    std::atomic<double> activeSeekTimeSec_{kNoSeekTime};
    std::atomic<uint8_t> strategy_{static_cast<uint8_t>(SeekStrategy::SeekToKeyframe)};
    std::atomic<double> gopKeyframePts_{kNoSeekTime};
    std::atomic<double> gopEndPts_{kNoSeekTime};
    std::atomic<bool> isSeekPending_{false};
    std::atomic<bool> isVideoSeeked_{false};
    std::atomic<bool> isAudioSeeked_{false};
    std::atomic<int32_t> maxSkipFrames_{0};
};

}  // namespace slark
