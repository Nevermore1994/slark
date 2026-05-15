//
// Created by Nevermore on 2025/3/9.
//

#pragma once

#include "NativeDecoderManager.h"

namespace slark {

class AudioHardwareDecoder : public NativeDecoder,
                             public std::enable_shared_from_this<AudioHardwareDecoder> {
public:
    AudioHardwareDecoder() : NativeDecoder(DecoderType::AACHardwareDecoder) {}

    ~AudioHardwareDecoder() override = default;

    Error decode(AVFrameRefPtr& frame) noexcept override;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void flush() noexcept override;

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::AACHardwareDecoder,
            Reflection::enroll<AudioHardwareDecoder>("AudioHardwareDecoder")};
        return info;
    }

    void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept override;
};

} // namespace slark
