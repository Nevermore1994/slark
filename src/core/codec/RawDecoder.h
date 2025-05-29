//
//  RawDecoder.hpp
//  slark
//
//  Created by Nevermore on 2022/4/26.
//

#pragma once

#include "IDecoder.h"
#include "MediaDefs.h"

namespace slark {

class RawDecoder : public IDecoder {

public:
    RawDecoder()
        : IDecoder(DecoderType::RAW) {
    }
    ~RawDecoder() override = default;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void close() noexcept override;

    void reset() noexcept override;

    void flush() noexcept override;

    DecoderErrorCode decode(AVFrameRefPtr& frame) noexcept override;

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::RAW,
            BaseClass::registerClass<RawDecoder>(GetClassName(RawDecoder))
        };
        return info;
    }
};

}
