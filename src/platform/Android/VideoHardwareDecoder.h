//
// Created by Nevermore on 2025/3/6.
//

#pragma once
#include "NativeDecoderManager.h"

namespace slark {

class VideoHardwareDecoder: public NativeDecoder,
        public std::enable_shared_from_this<VideoHardwareDecoder> {
public:
    VideoHardwareDecoder()
        : NativeDecoder(DecoderType::VideoHardWareDecoder) {
    }
    ~VideoHardwareDecoder() override = default;
public:
    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    DecoderErrorCode decode(AVFrameRefPtr& frame) noexcept override;
};

} // slark

