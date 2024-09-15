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
    ~RawDecoder() override = default;

    void open() noexcept override;

    void close() noexcept override;

    void reset() noexcept override;

    AVFrameArray flush() noexcept override;

    AVFrameArray send(AVFrameArray&& frameList) override;

    inline static const DecoderInfo& info() noexcept {
        static DecoderInfo info = {
            DecoderType::RAW,
            MEDIA_MIMETYPE_AUDIO_RAW,
            BaseClass<RawDecoder>::registerClass()
        };
        return info;
    }
};

}
