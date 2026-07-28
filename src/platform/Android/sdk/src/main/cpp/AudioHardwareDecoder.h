//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NativeDecoderManager.h"

namespace slark {

class AudioHardwareDecoder : public NativeDecoder,
                             public std::enable_shared_from_this<AudioHardwareDecoder> {
 public:
    AudioHardwareDecoder() : NativeDecoder(DecoderType::AACHardwareDecoder) {}

    ~AudioHardwareDecoder() override = default;

    Error decode(AVFrameRefPtr& frame) noexcept override;

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void flush() noexcept override;

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::AACHardwareDecoder,
            Reflection::enroll<AudioHardwareDecoder>("AudioHardwareDecoder")};
        return info;
    }

    void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept override;

 private:
    void retryPendingEOS() noexcept;

    AVFrameRefPtr pendingEOSFrame_;  ///< 缓存 sendPacket(EOS) 失败后被 codec 拒绝的 EOS 帧，在
                                     ///< receiveDecodedData 回调后重试
};

}  // namespace slark
