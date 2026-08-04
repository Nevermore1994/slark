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

    // 缓存 sendPacket(EOS) 失败后被 codec 拒绝的 EOS 帧，在 receiveDecodedData 回调后重试
    AVFrameRefPtr pendingEOSFrame_;
};

}  // namespace slark
