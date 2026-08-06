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
    double demuxHighWatermarkDuration = 10.0;      // decoded-queue high watermark triggering pause IO/demux (s)
    double demuxLowWatermarkDuration = 3.0;        // decoded-queue low watermark triggering resume IO/demux (s)
    double startBufferDuration = 2.0;              // startup prebuffer / post-stall rebuffer target (s)
    double rebufferScale = 1.5;                    // startBufferDuration multiplier after a stall
    int32_t audioDecoderHighWatermarkFrames = 16;  // audio render-queue high watermark pausing the decoder (frames)
    int32_t audioDecoderLowWatermarkFrames = 4;    // audio render-queue low watermark resuming the decoder (frames)
    int32_t videoDecoderHighWatermarkFrames = 8;   // video render-queue high watermark pausing the decoder (frames)
    int32_t videoDecoderLowWatermarkFrames = 2;    // video render-queue low watermark resuming the decoder (frames)
    int32_t maxDropPerVsync = 8;                   // max chase-drop frames per vsync, prevents vsync timeout
    int64_t seekFastForwardMs = 100;               // frame-skip window (ms)
    double avSyncThresholdSec = 0.1;               // A/V sync threshold (s); chase starts when video lags beyond it
    bool isLiveSource = false;                     // whether the source is live
    bool isNetworkSource = false;                  // whether the source is network (HTTP/HLS)

    [[nodiscard]] static BackpressureConfig forLocal() noexcept {
        BackpressureConfig cfg;
        cfg.demuxHighWatermarkDuration = 30.0;
        cfg.demuxLowWatermarkDuration = 2.0;
        cfg.startBufferDuration = 0.0;
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
        cfg.startBufferDuration = 2.0;
        cfg.rebufferScale = 1.5;
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
        cfg.startBufferDuration = 1.0;
        cfg.rebufferScale = 1.5;
        cfg.audioDecoderHighWatermarkFrames = 8;
        cfg.audioDecoderLowWatermarkFrames = 2;
        cfg.videoDecoderHighWatermarkFrames = 4;
        cfg.videoDecoderLowWatermarkFrames = 1;
        cfg.maxDropPerVsync = 4;
        cfg.avSyncThresholdSec = 0.08;
        cfg.isLiveSource = true;
        cfg.isNetworkSource = true;
        return cfg;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return demuxLowWatermarkDuration < demuxHighWatermarkDuration &&
               startBufferDuration < demuxHighWatermarkDuration &&
               audioDecoderLowWatermarkFrames < audioDecoderHighWatermarkFrames &&
               videoDecoderLowWatermarkFrames < videoDecoderHighWatermarkFrames;
    }
};

}  // namespace slark
