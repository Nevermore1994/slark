//
// Created by Nevermore on 2025/3/6.
//

#pragma once

#include "NativeDecoderManager.h"
#include "VideoDecodeResource.h"
#include "DecoderConfig.h"

namespace slark {

class VideoHardwareDecoder : public NativeDecoder,
                             public std::enable_shared_from_this<VideoHardwareDecoder> {
public:
    explicit VideoHardwareDecoder(DecodeMode mode = DecodeMode::Texture)
        : NativeDecoder(DecoderType::VideoHardWareDecoder),
          mode_(mode) {
    }

    ~VideoHardwareDecoder() noexcept override;

public:
    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    DecoderErrorCode decode(AVFrameRefPtr &frame) noexcept override;

    inline static const DecoderTypeInfo &info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::RAW,
            BaseClass::registerClass<VideoHardwareDecoder>(GetClassName(VideoHardwareDecoder))
        };
        return info;
    }

    void receiveDecodedData(
        DataPtr data,
        int64_t pts,
        bool isCompleted
    ) noexcept override;

    void flush() noexcept override;
private:
    DecoderErrorCode sendPacket(AVFrameRefPtr &frame) noexcept;

    DecoderErrorCode decodeByteBufferMode(AVFrameRefPtr &frame) noexcept;

    DecoderErrorCode decodeTextureMode(AVFrameRefPtr &frame) noexcept;

    void requestDecodeVideoFrame() noexcept;

    void initResource() noexcept;
private:
    DecodeMode mode_;
    EGLSurface surface_ = nullptr;
    std::shared_ptr<VideoDecodeResource> resource_ = nullptr;
    std::unordered_set<int64_t> discardPackets_;
};

} // slark

