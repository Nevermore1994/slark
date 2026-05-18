//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "VideoHardwareDecoder.h"

#include "DecoderConfig.h"
#include "FrameBuffer.h"
#include "GLContextManager.h"
#include "Log.hpp"
#include "NativeHardwareDecoder.h"
#include "Texture.h"
#include "TexturePool.h"

namespace slark {

Error VideoHardwareDecoder::decode(AVFrameRefPtr& frame) noexcept {
    assert(frame && frame->isVideo());
    if (!isOpen_) {
        LogE("decoder is not open");
        return Error::CodecNotStarted;
    }
    if (mode_ == DecodeMode::ByteBuffer) {
        return decodeByteBufferMode(frame);
    } else {
        return decodeTextureMode(frame);
    }
}

Error VideoHardwareDecoder::sendPacket(AVFrameRefPtr& frame) noexcept {
    NativeDecodeFlag flag = NativeDecodeFlag::None;
    auto frameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frame->info);
    if (frameInfo->isIDRFrame) {
        flag = NativeDecodeFlag::KeyFrame;
    } else if (frameInfo->isEndOfStream) {
        flag = NativeDecodeFlag::EndOfStream;
    }
    auto inputPts = int64_t(frame->ptsTime() * 1000000);
    auto res = NativeHardwareDecoder::sendPacket(decoderId_,
                                                 frame->data,
                                                 inputPts,
                                                 flag,
                                                 PacketType::Video);
    if (res != Error::Success && res != Error::CodecAgain) {
        LogE("send video packet failed, {}", toString(res));
    } else {
        LogI("send video packet success:{}, pts:{}, dts:{}, isDiscard:{}",
             toString(res),
             frame->ptsTime(),
             frame->dtsTime(),
             frame->isDiscard);
        if (frame->isDiscard) {
            discardPackets_.insert(inputPts);
        }
    }
    return res;
}

Error VideoHardwareDecoder::decodeByteBufferMode(AVFrameRefPtr& frame) noexcept {
    return sendPacket(frame);
}

void VideoHardwareDecoder::receiveDecodedData(DataPtr data,
                                              int64_t pts,
                                              bool isCompleted) noexcept {
    isCompleted_ = isCompleted;
    if (isCompleted) {
        LogI("decode video completed");
    }
    if (!data || data->empty()) {
        LogE("data is empty");
        return;
    }
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    auto frame = std::make_unique<AVFrame>(AVFrameType::Video);
    frame->pts = pts;
    frame->timeScale = 1000000;
    frame->data = std::move(data);
    auto videoFrameInfo = std::make_shared<VideoFrameInfo>();
    videoFrameInfo->format = FrameFormat::MediaCodecByteBuffer;
    videoFrameInfo->width = videoConfig->width;
    videoFrameInfo->height = videoConfig->height;
    frame->info = std::move(videoFrameInfo);
    invokeReceiveFunc(DecoderData{std::move(frame)});
}

Error VideoHardwareDecoder::decodeTextureMode(AVFrameRefPtr& frame) noexcept {
    if (!resource_->isValid()) {
        LogE("resource is invalid");
        return Error::CodecNotProcessed;
    }
    auto res = sendPacket(frame);
    if (res == Error::Success) {
        pendingFrameCount_.fetch_add(1, std::memory_order_release);
        frameReadyCv_.notify_one();
    }
    return res;
}

void VideoHardwareDecoder::onVideoSizeChanged(uint32_t width, uint32_t height) noexcept {
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    if (!videoConfig) {
        return;
    }
    LogI("video size changed: {}x{} -> {}x{}",
         videoConfig->width,
         videoConfig->height,
         width,
         height);
    // frameWorkerLoop 持续读取 width/height，加锁保证可见性
    std::lock_guard lock(frameReadyMutex_);
    videoConfig->width = width;
    videoConfig->height = height;
}

void VideoHardwareDecoder::flush() noexcept {
    NativeDecoder::flush();
    discardPackets_.clear();
    isCompleted_ = false;
    // 清空待处理帧计数，避免 flush 后帧处理线程消费到旧帧
    pendingFrameCount_.store(0, std::memory_order_release);
}

void VideoHardwareDecoder::startFrameWorker() noexcept {
    frameWorkerRunning_.store(true, std::memory_order_release);
    frameWorker_ = std::thread([this] {
        frameWorkerLoop();
    });
}

void VideoHardwareDecoder::stopFrameWorker() noexcept {
    frameWorkerRunning_.store(false, std::memory_order_release);
    frameReadyCv_.notify_all();
    if (frameWorker_.joinable()) {
        frameWorker_.join();
    }
}

void VideoHardwareDecoder::restoreTexture(TexturePtr texture) noexcept {
    if (!texture) {
        return;
    }
    if (auto manager = texture->manager().lock()) {
        manager->restore(std::move(texture));
    }
}

std::shared_ptr<AVFrame> VideoHardwareDecoder::buildOutputFrame(FrameBuffer* glFb,
                                                                int64_t pts,
                                                                uint32_t width,
                                                                uint32_t height) noexcept {
    if (!glFb) {
        return nullptr;
    }
    // release() 转移所有权给裸指针，接收端负责 delete 恢复
    auto texture = glFb->detachTexture();
    if (!texture) {
        return nullptr;
    }
    auto frame = std::make_shared<AVFrame>(AVFrameType::Video);
    frame->pts = pts;
    frame->timeScale = 1000000;
    // release() 转移所有权，渲染侧用完后通过 Texture::releaseResource 归还
    frame->opaque = texture.release();
    auto videoFrameInfo = std::make_shared<VideoFrameInfo>();
    videoFrameInfo->width = width;
    videoFrameInfo->height = height;
    videoFrameInfo->format = FrameFormat::MediaCodecSurface;
    frame->info = std::move(videoFrameInfo);
    return frame;
}

void VideoHardwareDecoder::frameWorkerLoop() noexcept {
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    // 等待时间 = 1.5 倍帧时长，fps 未知时用 100ms 保守上界
    const int64_t waitTime =
        videoConfig->fps > 0 ? static_cast<int64_t>(1500.0 / videoConfig->fps) : 100;

    resource_->attachContext(surface_);

    while (frameWorkerRunning_.load(std::memory_order_acquire)) {
        uint32_t curWidth = 0;
        uint32_t curHeight = 0;
        {
            std::unique_lock lock(frameReadyMutex_);
            frameReadyCv_.wait(lock, [this] {
                return !frameWorkerRunning_.load(std::memory_order_acquire) ||
                       pendingFrameCount_.load(std::memory_order_acquire) > 0;
            });
            // 在锁内快照，避免与 onVideoSizeChanged 的数据竞争
            curWidth = videoConfig->width;
            curHeight = videoConfig->height;
        }
        if (!frameWorkerRunning_.load(std::memory_order_acquire)) {
            break;
        }
        // flush 可能将计数清零，避免下溢
        if (pendingFrameCount_.load(std::memory_order_acquire) <= 0) {
            continue;
        }
        pendingFrameCount_.fetch_sub(1, std::memory_order_acq_rel);

        auto frameBuffer = resource_->acquire(curWidth, curHeight);
        glViewport(0, 0, static_cast<int32_t>(curWidth), static_cast<int32_t>(curHeight));
        if (!frameBuffer->bind(true)) {
            LogE("frame buffer bind failed");
            continue;
        }

        auto result =
            NativeHardwareDecoder::requestVideoFrame(decoderId_, waitTime, curWidth, curHeight);
        frameBuffer->unbind(true);

        if (result.isCompleted) {
            isCompleted_ = true;
            LogI("decode video completed");
        }
        if (!result.isAvailable) {
            LogE("requestVideoFrame unavailable, pts:{}", result.pts);
            continue;
        }

        auto* glFb = frameBuffer.get();
        if (discardPackets_.contains(result.pts)) {
            restoreTexture(glFb ? glFb->detachTexture() : nullptr);
            discardPackets_.erase(result.pts);
            LogI("decode success, discard:{}", result.pts);
            continue;
        }

        auto outputFrame = buildOutputFrame(glFb, result.pts, curWidth, curHeight);
        if (!outputFrame) {
            LogE("buildOutputFrame failed, pts:{}", result.pts);
            continue;
        }
        LogI("frame worker output frame, pts:{}", result.pts);
        invokeReceiveFunc(DecoderData{std::move(outputFrame)});
    }

    resource_->detachContext();
}

void VideoHardwareDecoder::initResource() noexcept {
    if (resource_) {
        return;
    }
    resource_ = VideoDecodeResourceManager::shareInstance().find(config_->playerId);
    if (!resource_) {
        resource_ = std::make_shared<VideoDecodeResource>(config_->playerId);
        resource_->init();
    } else {
        VideoDecodeResourceManager::shareInstance().remove(config_->playerId);
    }
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    surface_ = resource_->createOffscreenSurface(videoConfig->width, videoConfig->height);
}

VideoHardwareDecoder::~VideoHardwareDecoder() noexcept {
    stopFrameWorker();
    if (resource_) {
        if (surface_) {
            resource_->releaseSurface(surface_);
        }
        resource_.reset();
    }
    NativeDecoderManager::shareInstance().remove(decoderId_);
    decoderId_.clear();
}

bool VideoHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config);
    if (!videoConfig) {
        LogE("video decoder config is invalid");
        return false;
    }
    config_ = config;
    if (mode_ == DecodeMode::Texture) {
        initResource();
        if (!resource_->isValid()) {
            LogE("init resource failed");
            return false;
        }
        // open 阶段只做一次 attach 用于创建解码器，之后由帧处理线程持有 context
        resource_->attachContext(surface_);
    }
    decoderId_ = NativeHardwareDecoder::createVideoDecoder(videoConfig, mode_);
    if (decoderId_.empty()) {
        LogE("create video decoder failed!");
        return false;
    }
    NativeDecoderManager::shareInstance().add(decoderId_, shared_from_this());
    if (mode_ == DecodeMode::Texture) {
        resource_->detachContext();
        startFrameWorker();
    }
    isOpen_ = true;
    return true;
}

} // namespace slark
