//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NonCopyable.h"
#include "Time.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

namespace slark {

class SeekCoordinator : public NonCopyMovable {
public:
    SeekCoordinator() = default;
    ~SeekCoordinator() override = default;

    [[nodiscard]] bool beginSeek(CTime target) noexcept {
        bool superseded = isSeekPending_.load(std::memory_order_acquire);
        seekEpoch_.fetch_add(1, std::memory_order_acq_rel);
        // release store 保证读方看到新 epoch 时 seekTime 已更新
        activeSeekTimeSec_.store(target.second(), std::memory_order_release);
        isSeekPending_.store(true, std::memory_order_release);
        isVideoSeeked_.store(false, std::memory_order_release);
        isAudioSeeked_.store(false, std::memory_order_release);
        seekStartTimeNs_.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                               std::memory_order_release);
        return superseded;
    }

    void completeSeek() noexcept {
        activeSeekTimeSec_.store(kNoSeekTime, std::memory_order_release);
        isSeekPending_.store(false, std::memory_order_release);
        seekStartTimeNs_.store(kNoSeekStartTime, std::memory_order_release);
    }

    [[nodiscard]] bool isSeekPending() const noexcept {
        return isSeekPending_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isSeekTimedOut(std::chrono::milliseconds timeout) const noexcept {
        auto startNs = seekStartTimeNs_.load(std::memory_order_acquire);
        if (!isSeekPending_.load(std::memory_order_acquire) || startNs == kNoSeekStartTime) {
            return false;
        }
        using namespace std::chrono;
        auto startTp = steady_clock::time_point{nanoseconds{startNs}};
        return (steady_clock::now() - startTp) >= timeout;
    }

    [[nodiscard]] std::optional<double> activeSeekTime() const noexcept {
        double t = activeSeekTimeSec_.load(std::memory_order_acquire);
        if (t < 0.0) {
            return std::nullopt;
        }
        return t;
    }

    [[nodiscard]] uint64_t seekEpoch() const noexcept {
        return seekEpoch_.load(std::memory_order_acquire);
    }

    void onVideoSeeked() noexcept {
        isVideoSeeked_.store(true, std::memory_order_release);
    }

    void onAudioSeeked() noexcept {
        isAudioSeeked_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool isSeekCompleted(bool hasVideo, bool hasAudio) const noexcept {
        if (!isSeekPending_.load(std::memory_order_acquire)) {
            return true;
        }
        return (!hasVideo || isVideoSeeked_.load(std::memory_order_acquire)) &&
               (!hasAudio || isAudioSeeked_.load(std::memory_order_acquire));
    }

private:
    static constexpr double kNoSeekTime = -1.0;
    static constexpr int64_t kNoSeekStartTime = -1;

    std::atomic<uint64_t> seekEpoch_{0};
    std::atomic<double> activeSeekTimeSec_{kNoSeekTime};
    std::atomic<bool> isSeekPending_{false};
    std::atomic<bool> isVideoSeeked_{false};
    std::atomic<bool> isAudioSeeked_{false};
    std::atomic<int64_t> seekStartTimeNs_{kNoSeekStartTime};
};

} // namespace slark
