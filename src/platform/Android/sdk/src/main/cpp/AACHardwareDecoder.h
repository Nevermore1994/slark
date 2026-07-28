//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NativeDecoderManager.h"

namespace slark {

class AACHardwareDecoder : public NativeDecoder,
                           public std::enable_shared_from_this<AACHardwareDecoder> {
 public:
    AACHardwareDecoder() : NativeDecoder(DecoderType::AACHardwareDecoder) {}

    ~AACHardwareDecoder() override = default;

    Error decode(AVFrameRefPtr& frame) noexcept override;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void flush() noexcept override;

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::AACHardwareDecoder,
            Reflection::enroll<AACHardwareDecoder>("AACHardwareDecoder")};
        return info;
    }

    void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept override;
};

}  // namespace slark
