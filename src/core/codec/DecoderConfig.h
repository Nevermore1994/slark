//
// Created by Nevermore on 2024/7/28.
// slark DecoderConfig
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once

#include "IDecoder.h"
#include "VideoInfo.h"
#include "AudioInfo.h"

namespace slark {

struct AudioDecoderConfig: public DecoderConfig {
    uint16_t channels{};
    uint16_t bitsPerSample{};
    uint64_t sampleRate{};
    uint32_t timeScale{};

    void initWithAudioInfo(const std::shared_ptr<AudioInfo>& audioInfo) noexcept {
        if (!audioInfo) {
            return;
        }
        mediaInfo = audioInfo->mediaInfo;
        channels = audioInfo->channels;
        bitsPerSample = audioInfo->bitsPerSample;
        sampleRate = audioInfo->sampleRate;
        timeScale = audioInfo->timeScale;
        profile = static_cast<uint8_t>(audioInfo->profile);
    }
};

struct VideoDecoderConfig: public DecoderConfig {
    uint16_t naluHeaderLength{};
    uint32_t width{};
    uint32_t height{};
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
        width = videoInfo->width;
        height = videoInfo->height;
        sps = videoInfo->sps;
        pps = videoInfo->pps;
        vps = videoInfo->vps;
        timeScale = videoInfo->timeScale;
        profile = videoInfo->profile;
        level = videoInfo->level;
        return true;
    }
};

}
