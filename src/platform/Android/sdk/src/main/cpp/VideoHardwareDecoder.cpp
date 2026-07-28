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
    return sendPacket(frame);
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
    auto res = NativeHardwareDecoder::sendPacket(decoderId_, frame->data, inputPts, flag,
                                                 PacketType::Video);
    if (res != Error::Success && res != Error::CodecAgain && res != Error::CodecNotProcessed) {
        LogE("send video packet failed, {}", toString(res));
    } else {
        LogI("send video packet {}, index:{}, pts:{}, dts:{}, isDiscard:{}, frameType:{}",
             toString(res), frame->index, frame->ptsTime(), frame->dtsTime(), frame->isDiscard,
             static_cast<int>(frameInfo->frameType));
        if (frame->isDiscard) {
            discardPackets_.insert(inputPts);
        }
    }
    // EOS 被 codec 拒绝（输入队列满）时缓存，等排空输出后重试
    if (frameInfo->isEndOfStream && res == Error::CodecNotProcessed) {
        pendingEOSFrame_ = frame;
    }
    return res;
}

void VideoHardwareDecoder::retryPendingEOS() noexcept {
    if (!pendingEOSFrame_) {
        return;
    }
    auto frameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(pendingEOSFrame_->info);
    if (!frameInfo || !frameInfo->isEndOfStream) {
        pendingEOSFrame_.reset();
        return;
    }
    auto inputPts = int64_t(pendingEOSFrame_->ptsTime() * 1000000);
    auto res = NativeHardwareDecoder::sendPacket(decoderId_, pendingEOSFrame_->data, inputPts,
                                                 NativeDecodeFlag::EndOfStream, PacketType::Video);
    if (res == Error::Success) {
        LogI("pending EOS retry success");
        pendingEOSFrame_.reset();
    }
    // CodecNotProcessed / CodecAgain → 保持 pendingEOSFrame_，下次排空输出后继续重试
}

void VideoHardwareDecoder::receiveDecodedData(DataPtr data,
                                              int64_t pts,
                                              bool isCompleted) noexcept {
    isCompleted_ = isCompleted;
    if (isCompleted) {
        LogI("decode video completed");
    }

    if (mode_ == DecodeMode::ByteBuffer) {
        if (!data || data->empty()) {
            if (!isCompleted) {
                LogE("data is empty");
                return;
            }
        }
        auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
        auto frame = std::make_unique<AVFrame>(AVFrameType::Video);
        frame->pts = pts;
        frame->timeScale = 1000000;
        frame->data = std::move(data);
        if (discardPackets_.contains(pts)) {
            frame->isDiscard = true;
            discardPackets_.erase(pts);
        }
        auto videoFrameInfo = std::make_shared<VideoFrameInfo>();
        videoFrameInfo->format = FrameFormat::MediaCodecByteBuffer;
        videoFrameInfo->width = videoConfig->width;
        videoFrameInfo->height = videoConfig->height;
        frame->info = std::move(videoFrameInfo);
        invokeReceiveFunc(DecoderData{std::move(frame), Error::Success, currentSeekEpoch_});
        retryPendingEOS();
    } else {
        LogD("receiveDecodedData: Texture mode, pendingOutput_ -> true, pts={:.6f}",
             static_cast<double>(pts) / 1000000.0);
        pendingOutput_.store(true, std::memory_order_release);
        notifyWakeup();
    }
}

void VideoHardwareDecoder::onVideoSizeChanged(uint32_t width, uint32_t height) noexcept {
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    if (!videoConfig) {
        return;
    }
    LogI("video size changed: {}x{} -> {}x{}", videoConfig->width, videoConfig->height, width,
         height);
    videoConfig->width = width;
    videoConfig->height = height;
    renderWidth_.store(width, std::memory_order_release);
    renderHeight_.store(height, std::memory_order_release);
}

void VideoHardwareDecoder::flush() noexcept {
    pendingOutput_.store(false, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    NativeDecoder::flush();
    discardPackets_.clear();
    isCompleted_ = false;
    pendingEOSFrame_.reset();
}

void VideoHardwareDecoder::close() noexcept {
    isOpen_ = false;
    if (!decoderId_.empty()) {
        NativeHardwareDecoder::release(decoderId_);
        NativeDecoderManager::shareInstance().remove(decoderId_);
        decoderId_.clear();
    }
    if (resource_) {
        if (contextAttached_) {
            resource_->detachContext();
            contextAttached_ = false;
        }
        if (surface_) {
            resource_->releaseSurface(surface_);
            surface_ = nullptr;
        }
        resource_.reset();
    }
}

void VideoHardwareDecoder::stop() noexcept {
    close();
}

VideoHardwareDecoder::~VideoHardwareDecoder() noexcept {
    if (!decoderId_.empty()) {
        NativeHardwareDecoder::release(decoderId_);
        NativeDecoderManager::shareInstance().remove(decoderId_);
        decoderId_.clear();
    }
    if (resource_) {
        if (contextAttached_) {
            resource_->detachContext();
        }
        if (surface_) {
            resource_->releaseSurface(surface_);
            surface_ = nullptr;
        }
        resource_.reset();
    }
}

bool VideoHardwareDecoder::tryAquireFrame(int64_t& pts, bool& isCompleted) noexcept {
    constexpr uint64_t kRenderTimeoutMs = 1;
    auto result = NativeHardwareDecoder::requestVideoFrame(
        decoderId_, kRenderTimeoutMs, renderWidth_.load(std::memory_order_acquire),
        renderHeight_.load(std::memory_order_acquire));
    isCompleted = result.isCompleted;
    pts = result.pts;
    return result.isAvailable;
}

Error VideoHardwareDecoder::renderOutput() noexcept {
    if (mode_ != DecodeMode::Texture || !resource_ || !resource_->isValid()) {
        LogD("renderOutput: skip, mode={} resource={}", static_cast<int>(mode_),
             resource_ ? resource_->isValid() : false);
        return Error::CodecError;
    }
    if (!pendingOutput_.load(std::memory_order_acquire)) {
        LogD("renderOutput: no pending output, retry EOS");
        retryPendingEOS();
        return Error::CodecAgain;
    }

    if (!contextAttached_) {
        LogD("renderOutput: attach context");
        resource_->attachContext(surface_);
        contextAttached_ = true;
    }

    const uint32_t curWidth = renderWidth_.load(std::memory_order_acquire);
    const uint32_t curHeight = renderHeight_.load(std::memory_order_acquire);
    if (curWidth == 0 || curHeight == 0) {
        LogW("renderOutput: invalid size {}x{}", curWidth, curHeight);
        pendingOutput_.store(false, std::memory_order_release);
        return Error::CodecConfigInvalid;
    }

    auto frameBuffer = resource_->acquire(curWidth, curHeight);
    glViewport(0, 0, static_cast<int32_t>(curWidth), static_cast<int32_t>(curHeight));
    if (!frameBuffer->bind(true)) {
        LogE("renderOutput: frame buffer bind failed");
        pendingOutput_.store(false, std::memory_order_release);
        return Error::CodecError;
    }

    int64_t pts = 0;
    bool isCompleted = false;
    if (!tryAquireFrame(pts, isCompleted)) {
        frameBuffer->unbind(true);
        retryPendingEOS();
        LogD("renderOutput: tryAquireFrame no frame available, pendingOutput_={}",
             pendingOutput_.load());
        return Error::CodecAgain;
    }
    LogI("renderOutput: acquired frame pts={:.6f}, isCompleted={}, isDiscard={}",
         static_cast<double>(pts) / 1000000.0, isCompleted, discardPackets_.contains(pts));
    frameBuffer->unbind(true);

    if (isCompleted) {
        isCompleted_ = true;
    }

    // pendingOutput_ 保持 true：outputBufferQueue 中可能还有更多帧
    // processPendingOutputs 循环通过 hasPendingOutput() 继续尝试

    auto* glFb = frameBuffer.get();
    if (discardPackets_.contains(pts)) {
        discardPackets_.erase(pts);
        auto discardFrame = buildOutputFrame(glFb, pts, curWidth, curHeight);
        if (discardFrame) {
            discardFrame->isDiscard = true;
            invokeReceiveFunc(
                DecoderData{std::move(discardFrame), Error::Success, currentSeekEpoch_});
        } else {
            restoreTexture(glFb ? glFb->detachTexture() : nullptr);
        }
        retryPendingEOS();
        return Error::Success;
    }

    auto outputFrame = buildOutputFrame(glFb, pts, curWidth, curHeight);
    if (!outputFrame) {
        LogE("renderOutput: buildOutputFrame failed, pts:{:.6f}",
             static_cast<double>(pts) / 1000000.0);
        return Error::CodecError;
    }
    invokeReceiveFunc(DecoderData{std::move(outputFrame), Error::Success, currentSeekEpoch_});
    retryPendingEOS();
    return Error::Success;
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

bool VideoHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config);
    if (!videoConfig) {
        LogE("video decoder config is invalid");
        return false;
    }
    config_ = config;
    renderWidth_.store(videoConfig->width, std::memory_order_release);
    renderHeight_.store(videoConfig->height, std::memory_order_release);

    if (mode_ == DecodeMode::Texture) {
        initResource();
        if (!resource_->isValid()) {
            LogE("init resource failed");
            return false;
        }
        resource_->attachContext(surface_);
    }

    decoderId_ = NativeHardwareDecoder::createVideoDecoder(videoConfig, mode_);
    if (decoderId_.empty()) {
        LogE("create video decoder failed!");
        if (mode_ == DecodeMode::Texture) {
            resource_->detachContext();
        }
        return false;
    }
    NativeDecoderManager::shareInstance().add(decoderId_, shared_from_this());

    if (mode_ == DecodeMode::Texture) {
        resource_->detachContext();
        contextAttached_ = false;
    }

    isOpen_ = true;
    return true;
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
    auto texture = glFb->detachTexture();
    if (!texture) {
        return nullptr;
    }
    auto frame = std::make_shared<AVFrame>(AVFrameType::Video);
    frame->pts = pts;
    frame->timeScale = 1000000;
    frame->opaque = texture.release();
    auto videoFrameInfo = std::make_shared<VideoFrameInfo>();
    videoFrameInfo->width = width;
    videoFrameInfo->height = height;
    videoFrameInfo->format = FrameFormat::MediaCodecSurface;
    frame->info = std::move(videoFrameInfo);
    return frame;
}

}  // namespace slark
