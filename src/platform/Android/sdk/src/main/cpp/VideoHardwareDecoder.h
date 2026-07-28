//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <unordered_set>

#include "DecoderConfig.h"
#include "FrameBuffer.h"
#include "NativeDecoderManager.h"
#include "VideoDecodeResource.h"

namespace slark {

class VideoHardwareDecoder : public NativeDecoder,
                             public std::enable_shared_from_this<VideoHardwareDecoder> {
 public:
    explicit VideoHardwareDecoder(DecodeMode mode = DecodeMode::Texture)
        : NativeDecoder(DecoderType::VideoHardwareDecoder), mode_(mode) {}

    ~VideoHardwareDecoder() noexcept override;

 public:
    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    Error decode(AVFrameRefPtr& frame) noexcept override;

    bool hasPendingOutput() const noexcept override {
        return mode_ == DecodeMode::Texture && pendingOutput_.load(std::memory_order_acquire);
    }

    Error renderOutput() noexcept override;

    void clearPendingOutput() noexcept override {
        pendingOutput_.store(false, std::memory_order_release);
    }

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::VideoHardwareDecoder,
            Reflection::enroll<VideoHardwareDecoder>("VideoHardwareDecoder")};
        return info;
    }

    void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept override;

    void onVideoSizeChanged(uint32_t width, uint32_t height) noexcept override;

    void flush() noexcept override;

    void close() noexcept override;

    void stop() noexcept;

 private:
    Error sendPacket(AVFrameRefPtr& frame) noexcept;

    bool tryAquireFrame(int64_t& pts, bool& isCompleted) noexcept;

    void initResource() noexcept;

    void restoreTexture(TexturePtr texture) noexcept;

    std::shared_ptr<AVFrame> buildOutputFrame(FrameBuffer* fb,
                                              int64_t pts,
                                              uint32_t width,
                                              uint32_t height) noexcept;

    void retryPendingEOS() noexcept;

 private:
    DecodeMode mode_;
    EGLSurface surface_ = nullptr;
    std::shared_ptr<VideoDecodeResource> resource_ = nullptr;
    std::unordered_set<int64_t> discardPackets_;
    AVFrameRefPtr
        pendingEOSFrame_;  ///< 缓存 sendPacket(EOS) 失败后被 codec 拒绝的 EOS 帧，在排空输出后重试

    std::atomic<bool> pendingOutput_{false};
    bool contextAttached_ = false;
    std::atomic<uint32_t> renderWidth_{0};
    std::atomic<uint32_t> renderHeight_{0};
};

}  // namespace slark
