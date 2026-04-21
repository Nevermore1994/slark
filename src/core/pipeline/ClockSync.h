//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NonCopyable.h"
#include "Time.hpp"

#include <atomic>

namespace slark {

// AV 同步动作
enum class AVSyncAction : uint8_t {
    Render, // 正常渲染当前帧
    Hold,   // 跳过本次渲染，等音频追上
    Drop,   // 丢弃当前帧（追帧）
};

class ClockSync : public NonCopyMovable {
public:
    struct Metrics {
        int32_t dropCount = 0;       // 追帧循环中累计丢帧数
        int32_t chaseRoundCount = 0; // 追帧循环累计轮数
        int64_t vsyncDurationUs = 0; // 最近一次 vsync 回调耗时（微秒）
    };

    ClockSync() = default;
    ~ClockSync() override = default;

    // teardown 闸门：vsync 回调检查此标志，置位后立即返回 nullptr，防止悬空访问
    void notifyTeardown() noexcept {
        teardownFlag_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool isTearingDown() const noexcept {
        return teardownFlag_.load(std::memory_order_acquire);
    }

    void setAudioTime(Time::TimePoint t) noexcept {
        audioTime_.store(t.point(), std::memory_order_release);
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
        return hasAudio ? audioTime() : videoTime();
    }

    void reset() noexcept {
        audioTime_.store(0, std::memory_order_release);
        videoTime_.store(0, std::memory_order_release);
        playbackRate_.store(static_cast<uint16_t>(kDefaultPlaybackRate * kPlaybackRateScale),
                            std::memory_order_release);
        teardownFlag_.store(false, std::memory_order_release);
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

    // 设置变速播放速率，范围[0.5, 2.0]，默认 1.0
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

    // diff = audioTime - videoTime；正数表示视频落后，负数表示视频超前
    [[nodiscard]] static AVSyncAction evaluateAVSync(double diffSec,
                                                     double playbackRate = 1.0) noexcept {
        // 变速模式下，diff 除以当前播放速率，使同步阈值与速率成比例缩放
        double scaledMinThreshold = kMinThreshold * playbackRate;
        double scaledMaxThreshold = kMaxThreshold * playbackRate;

        auto absDiff = std::abs(diffSec);

        // 同步阈值随速率缩放：高速播放时阈值更宽松，低速更严格
        if (absDiff <= scaledMinThreshold || absDiff >= scaledMaxThreshold) {
            // 差值在阈值内或过大（时钟异常），正常渲染
            return AVSyncAction::Render;
        }

        if (diffSec > scaledMinThreshold) {
            // 视频落后音频 → 追帧
            return AVSyncAction::Drop;
        }

        // 视频超前音频 → 保持，等音频追上
        return AVSyncAction::Hold;
    }

private:
    static constexpr double kMinPlaybackRate = 0.5;
    static constexpr double kMaxPlaybackRate = 2.0;
    static constexpr double kDefaultPlaybackRate = 1.0;
    static constexpr uint32_t kPlaybackRateScale = 1000; // 整数化精度

    static constexpr double kMinThreshold = 0.1;  // 100ms
    static constexpr double kMaxThreshold = 10.0; // 10s

    std::atomic<uint64_t> audioTime_{0};
    std::atomic<uint64_t> videoTime_{0};
    std::atomic<bool> teardownFlag_{false};
    std::atomic<uint16_t> playbackRate_{
        static_cast<uint16_t>(kDefaultPlaybackRate * kPlaybackRateScale)};
    std::atomic<int32_t> dropCount_{0};
    std::atomic<int32_t> chaseRoundCount_{0};
    std::atomic<int64_t> vsyncDurationUs_{0};
};

} // namespace slark
