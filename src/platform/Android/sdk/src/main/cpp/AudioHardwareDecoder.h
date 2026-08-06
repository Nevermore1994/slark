//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AudioHardwareDecoderBase.h"

namespace slark {

class AudioHardwareDecoder : public AudioHardwareDecoderBase<AudioHardwareDecoder> {
 public:
    AudioHardwareDecoder() : AudioHardwareDecoderBase(DecoderType::AACHardwareDecoder) {}

    ~AudioHardwareDecoder() override = default;

    Error decode(AVFrameRefPtr& frame) noexcept override;

    void flush() noexcept override;

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::AACHardwareDecoder,
            Reflection::enroll<AudioHardwareDecoder>("AudioHardwareDecoder")};
        return info;
    }

 protected:
    void onDecodedDataReady() noexcept override;

 private:
    void retryPendingEOS() noexcept;

    // EOS frame cached when sendPacket(EOS) is rejected by the codec, retried in receiveDecodedData
    AVFrameRefPtr pendingEOSFrame_;
};

}  // namespace slark
