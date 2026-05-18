//
// Created by Nevermore on 2025/3/6.
//

#pragma once

#include "DecoderConfig.h"
#include "FrameBuffer.h"
#include "NativeDecoderManager.h"
#include "VideoDecodeResource.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>

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

    inline static const DecoderTypeInfo& info() noexcept {
        static DecoderTypeInfo info = {
            DecoderType::VideoHardwareDecoder,
            Reflection::enroll<VideoHardwareDecoder>("VideoHardwareDecoder")};
        return info;
    }

    void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept override;

    void onVideoSizeChanged(uint32_t width, uint32_t height) noexcept override;

    void flush() noexcept override;

private:
    Error sendPacket(AVFrameRefPtr& frame) noexcept;

    Error decodeByteBufferMode(AVFrameRefPtr& frame) noexcept;

    Error decodeTextureMode(AVFrameRefPtr& frame) noexcept;

    /// 启动帧处理线程（Texture 模式专用）
    void startFrameWorker() noexcept;

    /// 停止帧处理线程
    void stopFrameWorker() noexcept;

    /// 帧处理线程主循环：等待帧就绪信号 → FBO 绘制 → invokeReceiveFunc
    void frameWorkerLoop() noexcept;

    void initResource() noexcept;

    /// 将 texture 归还 TexturePool
    void restoreTexture(TexturePtr texture) noexcept;

    /// 从 FBO 取出 texture，构造输出 AVFrame
    std::shared_ptr<AVFrame> buildOutputFrame(FrameBuffer* fb,
                                              int64_t pts,
                                              uint32_t width,
                                              uint32_t height) noexcept;

private:
    DecodeMode mode_;
    EGLSurface surface_ = nullptr;
    std::shared_ptr<VideoDecodeResource> resource_ = nullptr;
    std::unordered_set<int64_t> discardPackets_;

    // 帧处理线程（Texture 模式）
    std::thread frameWorker_;
    std::mutex frameReadyMutex_;
    std::condition_variable frameReadyCv_;
    std::atomic<int32_t> pendingFrameCount_{0};
    std::atomic_bool frameWorkerRunning_{false};
};

} // namespace slark
