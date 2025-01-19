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
    RawDecoder() {
        decoderType_ = DecoderType::RAW;
    }
    ~RawDecoder() override = default;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void close() noexcept override;

    void reset() noexcept override;

    void flush() noexcept override;

    bool send(AVFramePtr frame) override;

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::RAW,
            BaseClass<RawDecoder>::registerClass(GetClassName(RawDecoder))
        };
        return info;
    }
};

}
