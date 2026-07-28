//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <VideoToolbox/VideoToolbox.h>

#include "IDecoder.h"
#include "MediaDefs.h"

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

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::VideoHardwareDecoder,
            Reflection::enroll<VideoHardwareDecoder>("VideoHardwareDecoder")};
        return info;
    }

 private:
    bool createDecodeSession() noexcept;

 private:
    VTDecompressionSessionRef decodeSession_ = nullptr;
    CMFormatDescriptionRef videoFormatDescription_ = nullptr;
};

}  // namespace slark
