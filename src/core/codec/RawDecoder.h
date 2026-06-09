//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "IDecoder.h"
#include "MediaDefs.h"

namespace slark {

class RawDecoder : public IDecoder {
public:
    RawDecoder() : IDecoder(DecoderType::RAW) {}

    ~RawDecoder() override = default;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void close() noexcept override;

    void reset() noexcept override;

    void flush() noexcept override;

    Error decode(AVFrameRefPtr& frame) noexcept override;

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {DecoderType::RAW,
                                       Reflection::enroll<RawDecoder>("RawDecoder")};
        return info;
    }
};

} // namespace slark
