//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cassert>
#include <cstdint>

namespace slark {

enum class MediaSourceType : uint8_t {
    Local,
    Network,
    Live,
};

struct BackpressureConfig {
    double highWatermarkDuration = 10.0; // 触发 pause 的缓冲上限（秒）
    double lowWatermarkDuration = 3.0;   // 触发 resume 的缓冲下限（秒）
    double hardCapDuration = 20.0;       // 强制丢包的硬上限（秒）
    double startBufferDuration = 2.0;    // 起播预缓冲目标（秒）
    double rebufferDuration = 2.0;       // 卡顿后重新缓冲目标（秒）
    int32_t minPauseHoldMs = 300;        // pause 最小驻留时间（ms），防止频繁翻转
    int32_t minResumeHoldMs = 300;       // resume 最小驻留时间（ms）
    int32_t maxDropPerVsync = 8;         // 单次 vsync 最大追帧丢帧数，防止 vsync 超时
    double avSyncThresholdSec = 0.1;     // AV 同步阈值（秒），视频落后超过此值开始追帧
    bool isLiveSource = false;           // 是否为直播源

    [[nodiscard]] static BackpressureConfig forLocal() noexcept {
        BackpressureConfig cfg;
        cfg.highWatermarkDuration = 30.0;
        cfg.lowWatermarkDuration = 5.0;
        cfg.hardCapDuration = 60.0;
        cfg.startBufferDuration = 0.0;
        cfg.rebufferDuration = 1.0;
        cfg.maxDropPerVsync = 8;
        cfg.avSyncThresholdSec = 0.1;
        cfg.isLiveSource = false;
        return cfg;
    }

    [[nodiscard]] static BackpressureConfig forNetwork() noexcept {
        BackpressureConfig cfg;
        cfg.highWatermarkDuration = 10.0;
        cfg.lowWatermarkDuration = 3.0;
        cfg.hardCapDuration = 20.0;
        cfg.startBufferDuration = 2.0;
        cfg.rebufferDuration = 2.0;
        cfg.maxDropPerVsync = 8;
        cfg.avSyncThresholdSec = 0.1;
        cfg.isLiveSource = false;
        return cfg;
    }

    [[nodiscard]] static BackpressureConfig forLive() noexcept {
        BackpressureConfig cfg;
        cfg.highWatermarkDuration = 5.0;
        cfg.lowWatermarkDuration = 1.0;
        cfg.hardCapDuration = 10.0;
        cfg.startBufferDuration = 1.0;
        cfg.rebufferDuration = 1.0;
        cfg.maxDropPerVsync = 4; // 直播追帧更保守，避免画面跳跃
        cfg.avSyncThresholdSec = 0.08;
        cfg.isLiveSource = true;
        return cfg;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return lowWatermarkDuration < highWatermarkDuration &&
               hardCapDuration > highWatermarkDuration &&
               startBufferDuration < highWatermarkDuration;
    }
};

} // namespace slark
