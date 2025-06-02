//
// Created by Nevermore on 2025/3/6.
//

#pragma once

#include <string>
#include "Data.hpp"
#include "DecoderConfig.h"

namespace slark {

enum NativeDecodeFlag {
    None,
    KeyFrame = 1,
    CodecConfig = 2,
    EndOfStream = 4,
};

enum class DecoderErrorCode;

struct NativeHardwareDecoder {
    static std::string createVideoDecoder(
        const std::shared_ptr<VideoDecoderConfig> &decoderConfig,
        DecodeMode mode
    );

    static std::string createAudioDecoder(const std::shared_ptr<AudioDecoderConfig> &decoderConfig);

    static DecoderErrorCode sendPacket(
        std::string_view decoderId,
        DataPtr &data,
        uint64_t pts,
        NativeDecodeFlag flag
    );

    static void release(std::string_view decoderId);

    static void flush(std::string_view decoderId);

    static uint64_t requestVideoFrame(
        std::string_view decoderId,
        uint64_t waitTime,
        uint32_t width,
        uint32_t height
    );
};

}