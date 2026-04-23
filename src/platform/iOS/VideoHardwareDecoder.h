//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "IDecoder.h"
#include "MediaDefs.h"

#import <VideoToolbox/VideoToolbox.h>

namespace slark {

class VideoHardwareDecoder : public IDecoder {
public:
    VideoHardwareDecoder() : IDecoder(DecoderType::VideoHardwareDecoder) {}

    ~VideoHardwareDecoder() override;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;
    void close() noexcept override;
    void reset() noexcept override;
    void flush() noexcept override;
    Error decode(AVFrameRefPtr& frame) noexcept override;

    SLARK_DECODER_INFO(VideoHardwareDecoder, DecoderType::VideoHardwareDecoder)

private:
    bool createDecodeSession() noexcept;

private:
    VTDecompressionSessionRef decodeSession_ = nullptr;
    CMFormatDescriptionRef videoFormatDescription_ = nullptr;
};

} // namespace slark
