//
// Created by Nevermore on 2024/7/28.
// slark DecoderConfig
// Copyright (c) 2024 Nevermore All rights reserved.
//
#pragma once

#include "IDecoder.h"

namespace slark {

struct AudioDecoderConfig: public DecoderConfig {
    uint8_t profile{};
    uint16_t channels{};
    uint16_t bitsPerSample{};
    uint64_t sampleRate{};
    uint32_t timeScale{};
};

struct VideoDecoderConfig: public DecoderConfig {
    uint16_t naluHeaderLength{};
    uint32_t width{};
    uint32_t height{};
    DataRefPtr sps;
    DataRefPtr pps;
    DataRefPtr vps;
    
    ~VideoDecoderConfig() override = default;
};

}
