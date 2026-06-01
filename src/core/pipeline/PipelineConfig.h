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
    double demuxHighWatermarkDuration = 10.0;     // 待解码队列触发 pause IO/demux 的上限（秒）
    double demuxLowWatermarkDuration = 3.0;       // 待解码队列触发 resume IO/demux 的下限（秒）
    double hardCapDuration = 20.0;                // 强制丢包的硬上限（秒）
    double startBufferDuration = 2.0;             // 起播预缓冲目标（秒）
    double rebufferDuration = 2.0;                // 卡顿后重新缓冲目标（秒）
    int32_t audioDecoderHighWatermarkFrames = 16; // 待渲染音频队列触发暂停解码器的上限（帧）
    int32_t audioDecoderLowWatermarkFrames = 4;   // 待渲染音频队列触发恢复解码器的下限（帧）
    int32_t videoDecoderHighWatermarkFrames = 8;  // 待渲染视频队列触发暂停解码器的上限（帧）
    int32_t videoDecoderLowWatermarkFrames = 2;   // 待渲染视频队列触发恢复解码器的下限（帧）
    int32_t maxDropPerVsync = 8;                  // 单次 vsync 最大追帧丢帧数，防止 vsync 超时
    double avSyncThresholdSec = 0.1;              // AV 同步阈值（秒），视频落后超过此值开始追帧
    bool isLiveSource = false;                    // 是否为直播源
    bool isNetworkSource = false;                 // 是否为网络源（HTTP/HLS）

    [[nodiscard]] static BackpressureConfig forLocal() noexcept {
        BackpressureConfig cfg;
        cfg.demuxHighWatermarkDuration = 30.0;
        cfg.demuxLowWatermarkDuration = 5.0;
        cfg.hardCapDuration = 60.0;
        cfg.startBufferDuration = 0.0;
        cfg.rebufferDuration = 1.0;
        cfg.audioDecoderHighWatermarkFrames = 16;
        cfg.audioDecoderLowWatermarkFrames = 4;
        cfg.videoDecoderHighWatermarkFrames = 8;
        cfg.videoDecoderLowWatermarkFrames = 2;
        cfg.maxDropPerVsync = 8;
        cfg.avSyncThresholdSec = 0.1;
        cfg.isLiveSource = false;
        cfg.isNetworkSource = false;
        return cfg;
    }

    [[nodiscard]] static BackpressureConfig forNetwork() noexcept {
        BackpressureConfig cfg;
        cfg.demuxHighWatermarkDuration = 10.0;
        cfg.demuxLowWatermarkDuration = 3.0;
        cfg.hardCapDuration = 20.0;
        cfg.startBufferDuration = 2.0;
        cfg.rebufferDuration = 2.0;
        cfg.audioDecoderHighWatermarkFrames = 16;
        cfg.audioDecoderLowWatermarkFrames = 4;
        cfg.videoDecoderHighWatermarkFrames = 8;
        cfg.videoDecoderLowWatermarkFrames = 2;
        cfg.maxDropPerVsync = 8;
        cfg.avSyncThresholdSec = 0.1;
        cfg.isLiveSource = false;
        cfg.isNetworkSource = true;
        return cfg;
    }

    [[nodiscard]] static BackpressureConfig forLive() noexcept {
        BackpressureConfig cfg;
        cfg.demuxHighWatermarkDuration = 5.0;
        cfg.demuxLowWatermarkDuration = 1.0;
        cfg.hardCapDuration = 10.0;
        cfg.startBufferDuration = 1.0;
        cfg.rebufferDuration = 1.0;
        cfg.audioDecoderHighWatermarkFrames = 8;
        cfg.audioDecoderLowWatermarkFrames = 2;
        cfg.videoDecoderHighWatermarkFrames = 4;
        cfg.videoDecoderLowWatermarkFrames = 1;
        cfg.maxDropPerVsync = 4; // 直播追帧更保守，避免画面跳跃
        cfg.avSyncThresholdSec = 0.08;
        cfg.isLiveSource = true;
        cfg.isNetworkSource = true;
        return cfg;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return demuxLowWatermarkDuration < demuxHighWatermarkDuration &&
               hardCapDuration > demuxHighWatermarkDuration &&
               startBufferDuration < demuxHighWatermarkDuration &&
               audioDecoderLowWatermarkFrames < audioDecoderHighWatermarkFrames &&
               videoDecoderLowWatermarkFrames < videoDecoderHighWatermarkFrames;
    }
};

} // namespace slark
