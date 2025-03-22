//
// Created by Nevermore on 2025/3/6.
//

#pragma once

#include <string>
#include "Data.hpp"

namespace slark {

enum NativeDecodeFlag {
    None,
    KeyFrame = 1,
    CodecConfig = 2,
    EndOfStream = 4,
};

struct NativeHardwareDecoder {
    static std::string createVideo(const std::string& mediaInfo, int32_t width, int32_t height);

    static std::string createAudio(const std::string& mediaInfo, uint64_t sampleRate,
                                   uint8_t channelCount, uint8_t profile);

    static void sendData(std::string_view decoderId, DataPtr data, uint64_t pts, NativeDecodeFlag flag);

    static void release(std::string_view decoderId);

    static void flush(std::string_view decoderId);
};

}