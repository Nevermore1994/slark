//
// Created by Nevermore on 2025/3/6.
//

#pragma once

#include "DecoderConfig.h"
#include "NativeDecoderManager.h"
#include "VideoDecodeResource.h"

namespace slark {

class VideoHardwareDecoder : public NativeDecoder,
                             public std::enable_shared_from_this<VideoHardwareDecoder> {
public:
    explicit VideoHardwareDecoder(DecodeMode mode = DecodeMode::Texture)
        : NativeDecoder(DecoderType::VideoHardWareDecoder), mode_(mode) {}

    ~VideoHardwareDecoder() noexcept override;

public:
    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    Error decode(AVFrameRefPtr& frame) noexcept override;

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::VideoHardWareDecoder,
            BaseClass::registerClass<VideoHardwareDecoder>(GetClassName(VideoHardwareDecoder))};
        return info;
    }

    void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept override;

    void flush() noexcept override;

private:
    Error sendPacket(AVFrameRefPtr& frame) noexcept;

    Error decodeByteBufferMode(AVFrameRefPtr& frame) noexcept;

    Error decodeTextureMode(AVFrameRefPtr& frame) noexcept;

    void requestDecodeVideoFrame() noexcept;

    void initResource() noexcept;

private:
    DecodeMode mode_;
    EGLSurface surface_ = nullptr;
    std::shared_ptr<VideoDecodeResource> resource_ = nullptr;
    std::unordered_set<int64_t> discardPackets_;
};

} // namespace slark
