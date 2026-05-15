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

enum class SeekStrategy : uint8_t {
    FastForwardInGOP,
    SeekToGOPKeyframe,
    SeekToKeyframe,
};

class SeekContext : public NonCopyMovable {
public:
    SeekContext() = default;
    ~SeekContext() = default;

    /// 开始 seek，返回 true 表示覆盖了上一次未完成的 seek
    [[nodiscard]] bool beginSeek(CTime target, SeekStrategy strategy) noexcept {
        bool superseded = isSeekPending_.load(std::memory_order_acquire);
        seekEpoch_.fetch_add(1, std::memory_order_acq_rel);
        activeSeekTimeSec_.store(target.second(), std::memory_order_release);
        strategy_.store(static_cast<uint8_t>(strategy), std::memory_order_release);
        isSeekPending_.store(true, std::memory_order_release);
        isVideoSeeked_.store(false, std::memory_order_release);
        isAudioSeeked_.store(false, std::memory_order_release);
        seekStartTimeNs_.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                               std::memory_order_release);
        return superseded;
    }

    void updateGOPRange(double keyframePts, double gopEndPts) noexcept {
        gopKeyframePts_.store(keyframePts, std::memory_order_release);
        gopEndPts_.store(gopEndPts, std::memory_order_release);
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

    /// 完成 seek，返回 true 表示本次调用成功完成（CAS 语义，多线程安全）
    [[nodiscard]] bool completeSeek() noexcept {
        bool expected = true;
        if (!isSeekPending_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            return false;
        }
        activeSeekTimeSec_.store(kNoSeekTime, std::memory_order_release);
        seekStartTimeNs_.store(kNoSeekStartTime, std::memory_order_release);
        return true;
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

    /// CAS 语义：仅第一次调用返回 true，用于解码线程抢占 seek 首帧 push 权
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

    /// 捕获时的 epoch 与当前不一致，说明已发生新 seek
    [[nodiscard]] bool isEpochStale(uint64_t capturedEpoch) const noexcept {
        return capturedEpoch != seekEpoch_.load(std::memory_order_acquire);
    }

private:
    static constexpr double kNoSeekTime = -1.0;
    static constexpr int64_t kNoSeekStartTime = -1;

    std::atomic<uint64_t> seekEpoch_{0};
    std::atomic<double> activeSeekTimeSec_{kNoSeekTime};
    std::atomic<uint8_t> strategy_{static_cast<uint8_t>(SeekStrategy::SeekToKeyframe)};
    std::atomic<double> gopKeyframePts_{kNoSeekTime};
    std::atomic<double> gopEndPts_{kNoSeekTime};
    std::atomic<bool> isSeekPending_{false};
    std::atomic<bool> isVideoSeeked_{false};
    std::atomic<bool> isAudioSeeked_{false};
    std::atomic<int64_t> seekStartTimeNs_{kNoSeekStartTime};
};

} // namespace slark
