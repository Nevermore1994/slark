//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Data.h"
#include "DecoderConfig.h"
#include "Error.h"

#include <string>

namespace slark {

enum class PacketType : uint8_t {
    Video,
    Audio,
};

enum NativeDecodeFlag {
    None,
    KeyFrame = 1,
    CodecConfig = 2,
    EndOfStream = 4,
};

struct VideoFrameResult {
    bool isAvailable = false;
    bool isCompleted = false;
    int64_t pts = 0;

    static VideoFrameResult unpack(uint64_t packed) noexcept {
        VideoFrameResult r;
        r.isAvailable = (packed >> 63) & 1u;
        r.isCompleted = (packed >> 62) & 1u;
        auto maskedPts = packed & 0x3FFFFFFFFFFFFFFFull;
        if (maskedPts & (1ULL << 61)) {
            maskedPts |= 0xC000000000000000ULL;
        }
        r.pts = static_cast<int64_t>(maskedPts);
        return r;
    }
};

struct NativeHardwareDecoder {
    static std::string createVideoDecoder(const std::shared_ptr<VideoDecoderConfig>& decoderConfig,
                                          DecodeMode mode);

    static std::string createAudioDecoder(const std::shared_ptr<AudioDecoderConfig>& decoderConfig);

    static Error sendPacket(std::string_view decoderId,
                            DataPtr& data,
                            int64_t pts,
                            NativeDecodeFlag flag,
                            PacketType type);

    static void release(std::string_view decoderId);

    static void flush(std::string_view decoderId);

    static VideoFrameResult requestVideoFrame(std::string_view decoderId,
                                              uint64_t waitTime,
                                              uint32_t width,
                                              uint32_t height);
};

} // namespace slark