//
// Created by Nevermore on 2023/7/28.
// slark DecoderConfig
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once

#include "IDecoder.h"

namespace slark {

struct AudioDecoderConfig : public DecoderConfig {
    uint16_t channels{};
    uint16_t bitsPerSample{};
    uint64_t sampleRate{};
    uint32_t timeScale{};
};

enum class CodecType : uint8_t {
    H264,
    H265,
};

struct VideoDecoderConfig : public DecoderConfig {
    CodecType codec = CodecType::H264;
    uint16_t naluHeaderLength{};
    uint32_t width{};
    uint32_t height{};
    DataRefPtr sps;
    DataRefPtr pps;
    DataRefPtr vps;
    
    ~VideoDecoderConfig() override = default;
};

}
