//
// Created by Nevermore on 2025/3/6.
//

#pragma once

#include "NativeDecoderManager.h"
#include "AndroidEGLContext.h"
#include "FrameBufferPool.h"
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

    void initContext() noexcept;

    inline static const DecoderTypeInfo &info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::RAW,
            BaseClass::registerClass<VideoHardwareDecoder>(GetClassName(VideoHardwareDecoder))
        };
        return info;
    }

private:
    DecoderErrorCode sendPacket(AVFrameRefPtr &frame) noexcept;

    DecoderErrorCode decodeByteBufferMode(AVFrameRefPtr &frame) noexcept;

    DecoderErrorCode decodeTextureMode(AVFrameRefPtr &frame) noexcept;

    void requestVideoFrame() noexcept;

private:
    DecodeMode mode_;
    std::shared_ptr<AndroidEGLContext> context_;
    EGLSurface surface_ = nullptr;
    std::unique_ptr<FrameBufferPool> frameBufferPool_;
};

} // slark

