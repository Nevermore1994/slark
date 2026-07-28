//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "AudioInfo.h"
#include "IDecoder.h"
#include "VideoInfo.h"

namespace slark {

enum class DecodeMode {
    Texture,
    ByteBuffer,
};

struct AudioDecoderConfig : public DecoderConfig {
    uint16_t channels{};
    uint16_t bitsPerSample{};
    uint64_t sampleRate{};
    uint32_t timeScale{};
    uint32_t samplingFrequencyIndex{};
    uint32_t samplingFreqIndexExt{};
    uint8_t audioObjectTypeExt{};

    void initWithAudioInfo(const std::shared_ptr<AudioInfo>& audioInfo) noexcept {
        if (!audioInfo) {
            return;
        }
        mediaInfo = audioInfo->mediaInfo;
        channels = audioInfo->channels;
        bitsPerSample = audioInfo->bitsPerSample;
        sampleRate = audioInfo->sampleRate;
        timeScale = audioInfo->timeScale;
        samplingFrequencyIndex = audioInfo->samplingFrequencyIndex;
        samplingFreqIndexExt = audioInfo->samplingFreqIndexExt;
        audioObjectTypeExt = audioInfo->audioObjectTypeExt;
        profile = static_cast<uint8_t>(audioInfo->profile);
    }
};

struct VideoDecoderConfig : public DecoderConfig {
    uint16_t naluHeaderLength{};
    uint16_t fps{};
    uint32_t width{};
    uint32_t height{};
    uint8_t numRefFrames{};  // from SPS num_ref_frames
    DataRefPtr sps;
    DataRefPtr pps;
    DataRefPtr vps;

    ~VideoDecoderConfig() override = default;

    bool initWithVideoInfo(const std::shared_ptr<VideoInfo>& videoInfo) noexcept {
        if (!videoInfo) {
            return false;
        }
        mediaInfo = videoInfo->mediaInfo;
        naluHeaderLength = videoInfo->naluHeaderLength;
        fps = videoInfo->fps;
        width = videoInfo->width;
        height = videoInfo->height;
        sps = videoInfo->sps;
        pps = videoInfo->pps;
        vps = videoInfo->vps;
        timeScale = videoInfo->timeScale;
        profile = videoInfo->profile;
        level = videoInfo->level;
        numRefFrames = videoInfo->numRefFrames;
        return true;
    }
};

}  // namespace slark
