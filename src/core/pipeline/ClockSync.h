//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NonCopyable.h"
#include "Time.hpp"

#include <atomic>

namespace slark {

enum class AVSyncAction : uint8_t {
    Render,
    Hold,
    Drop,
};

class ClockSync : public NonCopyMovable {
public:
    struct Metrics {
        int32_t dropCount = 0;
        int32_t chaseRoundCount = 0;
        int64_t vsyncDurationUs = 0;
    };

    ClockSync() noexcept {
        audioLastUpdateUs_.store(Time::nowTimeStamp().point(), std::memory_order_release);
    }
    ~ClockSync() = default;

    void setAudioTime(Time::TimePoint t) noexcept {
        audioTime_.store(t.point(), std::memory_order_release);
        audioLastUpdateUs_.store(Time::nowTimeStamp().point(), std::memory_order_release);
    }

    void setVideoTime(Time::TimePoint t) noexcept {
        videoTime_.store(t.point(), std::memory_order_release);
    }

    [[nodiscard]] Time::TimePoint audioTime() const noexcept {
        return Time::TimePoint(audioTime_.load(std::memory_order_acquire));
    }

    [[nodiscard]] Time::TimePoint videoTime() const noexcept {
        return Time::TimePoint(videoTime_.load(std::memory_order_acquire));
    }

    [[nodiscard]] Time::TimePoint masterTime(bool hasAudio) const noexcept {
        if (!hasAudio) {
            return videoTime();
        }
        auto audioPts = audioTime_.load(std::memory_order_acquire);
        auto lastUpdate = audioLastUpdateUs_.load(std::memory_order_acquire);
        auto now = Time::nowTimeStamp().point();
        if (now > lastUpdate && (now - lastUpdate) > kAudioClockStaleThresholdUs) {
            auto elapsed = static_cast<double>(now - lastUpdate);
            auto rate = playbackRate();
            auto estimatedUs = static_cast<uint64_t>(elapsed * rate);
            return Time::TimePoint(audioPts + estimatedUs);
        }
        return Time::TimePoint(audioPts);
    }

    void reset() noexcept {
        audioTime_.store(0, std::memory_order_release);
        audioLastUpdateUs_.store(Time::nowTimeStamp().point(), std::memory_order_release);
        videoTime_.store(0, std::memory_order_release);
        playbackRate_.store(static_cast<uint16_t>(kDefaultPlaybackRate * kPlaybackRateScale),
                            std::memory_order_release);
        videoFps_.store(0, std::memory_order_release);
        dropCount_.store(0, std::memory_order_relaxed);
        chaseRoundCount_.store(0, std::memory_order_relaxed);
        vsyncDurationUs_.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] Metrics metrics() const noexcept {
        return Metrics{
            .dropCount = dropCount_.load(std::memory_order_relaxed),
            .chaseRoundCount = chaseRoundCount_.load(std::memory_order_relaxed),
            .vsyncDurationUs = vsyncDurationUs_.load(std::memory_order_relaxed),
        };
    }

    void recordDrop(int32_t count) noexcept {
        dropCount_.fetch_add(count, std::memory_order_relaxed);
        chaseRoundCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void recordVsyncDuration(int64_t us) noexcept {
        vsyncDurationUs_.store(us, std::memory_order_relaxed);
    }

    void setPlaybackRate(double rate) noexcept {
        if (rate >= kMinPlaybackRate && rate <= kMaxPlaybackRate) {
            playbackRate_.store(static_cast<uint16_t>(rate * kPlaybackRateScale),
                                std::memory_order_release);
        }
    }

    [[nodiscard]] double playbackRate() const noexcept {
        return playbackRate_.load(std::memory_order_acquire) /
               static_cast<double>(kPlaybackRateScale);
    }

    void setVideoFps(double fps) noexcept {
        if (fps > 0.0) {
            videoFps_.store(static_cast<uint32_t>(fps * kFpsScale), std::memory_order_release);
        }
    }

    [[nodiscard]] double videoFps() const noexcept {
        return videoFps_.load(std::memory_order_acquire) / static_cast<double>(kFpsScale);
    }

    static constexpr double kEarlyTolerance = 0.002;

    [[nodiscard]] static AVSyncAction evaluateAVSync(double diffSec,
                                                     double playbackRate = 1.0,
                                                     double earlyToleranceSec = 0.0) noexcept {
        const double scaledMinThreshold = kMinThreshold * playbackRate;

        if (diffSec > scaledMinThreshold) {
            return AVSyncAction::Drop;
        }

        if (diffSec < -(scaledMinThreshold + earlyToleranceSec)) {
            if (-diffSec >= kMaxThreshold) {
                return AVSyncAction::Render;
            }
            return AVSyncAction::Hold;
        }

        return AVSyncAction::Render;
    }

private:
    static constexpr double kMinPlaybackRate = 0.5;
    static constexpr double kMaxPlaybackRate = 2.0;
    static constexpr double kDefaultPlaybackRate = 1.0;
    static constexpr uint32_t kPlaybackRateScale = 1000;
    static constexpr uint32_t kFpsScale = 100;

    static constexpr double kMinThreshold = 0.04;
    static constexpr double kMaxThreshold = 10.0;
    static constexpr uint64_t kAudioClockStaleThresholdUs = 50000;

    std::atomic<uint64_t> audioTime_{0};
    std::atomic<uint64_t> audioLastUpdateUs_{0};
    std::atomic<uint64_t> videoTime_{0};
    std::atomic<uint16_t> playbackRate_{
        static_cast<uint16_t>(kDefaultPlaybackRate * kPlaybackRateScale)};
    std::atomic<uint32_t> videoFps_{0};
    std::atomic<int32_t> dropCount_{0};
    std::atomic<int32_t> chaseRoundCount_{0};
    std::atomic<int64_t> vsyncDurationUs_{0};
};

} // namespace slark
