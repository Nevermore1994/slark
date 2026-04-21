//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "VideoHardwareDecoder.h"

#include "DecoderConfig.h"
#include "GLContextManager.h"
#include "Log.hpp"
#include "NativeHardwareDecoder.h"
#include "OpenGLFrameBuffer.h"
#include "OpenGLSurface.h"
#include "Texture.h"
#include "TexturePool.h"

namespace slark {

Error VideoHardwareDecoder::decode(AVFrameRefPtr& frame) noexcept {
    assert(frame && frame->isVideo());
    if (!isOpen_) {
        LogE("decoder is not open");
        return Error::DecoderNotStarted;
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
    convert to microseconds auto res =
        NativeHardwareDecoder::sendPacket(decoderId_, frame->data, inputPts, flag);
    if (res != Error::Success && res != Error::DecoderAgain) {
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
    auto pts = frame->pts;
    auto res = sendPacket(frame);
    if (res != Error::Success && res != Error::DecoderAgain) {
        LogE("send packet failed, {}", toString(res));
    } else {
        LogI("send video packet success, pts:{}, dts:{}", pts, frame->dts);
    }
    return res;
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
    us frame->data = std::move(data);
    auto videoFrameInfo = std::make_shared<VideoFrameInfo>();
    videoFrameInfo->format = FrameFormat::MediaCodecByteBuffer;
    videoFrameInfo->width = videoConfig->width;
    videoFrameInfo->height = videoConfig->height;
    frame->info = std::move(videoFrameInfo);
    invokeReceiveFunc(std::move(frame));
}

Error VideoHardwareDecoder::decodeTextureMode(AVFrameRefPtr& frame) noexcept {
    if (!resource_->isValid()) {
        LogE("resource is invalid");
        return Error::DecoderNotProcessed;
    }
    resource_->attachContext(surface_.get());
    auto res = sendPacket(frame);
    if (res == Error::Success) {
        requestDecodeVideoFrame();
    }
    resource_->detachContext();
    return res;
}

void VideoHardwareDecoder::flush() noexcept {
    NativeDecoder::flush();
    discardPackets_.clear();
    isCompleted_ = false;
}

void VideoHardwareDecoder::requestDecodeVideoFrame() noexcept {
    constexpr int64_t kWaitTime = 3;
    10ms constexpr int32_t kMaxRetryCount = 3;
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    auto frameBuffer = resource_->acquire(videoConfig->width, videoConfig->height);
    glViewport(0, 0, static_cast<int>(videoConfig->width), static_cast<int>(videoConfig->height));
    if (eglGetError() != EGL_SUCCESS) {
        LogE("set view port error!");
        return;
    }
    if (!frameBuffer->bind(true)) {
        LogE("frame buffer bind failed");
        return;
    }
    if (eglGetError() != EGL_SUCCESS) {
        LogE("frame buffer bind error!");
        return;
    }
    int32_t retryCount = 0;
    bool isSuccess = false;
    bool isCompleted = false;
    int64_t pts = 0;
    do {
        retryCount++;
        auto result = NativeHardwareDecoder::requestVideoFrame(decoderId_,
                                                               kWaitTime,
                                                               videoConfig->width,
                                                               videoConfig->height);
        isSuccess = result.isAvailable;
        isCompleted = result.isCompleted;
        pts = result.pts;
        if (!isSuccess) {
            LogE("decode video frame failed:{}", retryCount);
        } else {
            auto* glFb = dynamic_cast<OpenGLFrameBuffer*>(frameBuffer.get());
            auto texId = glFb ? glFb->innerFrameBuffer()->texture()->textureId() : 0;
            LogI("decode video frame success:{}, {}", pts, texId);
            break;
        }
    } while (retryCount < kMaxRetryCount);
    frameBuffer->unbind(true);
    isCompleted_ = isCompleted;
    if (isCompleted) {
        LogI("decode video completed");
    }
    if (!isSuccess) {
        LogE("request decode video frame failed after {} retries", retryCount);
        return;
    }
    if (discardPackets_.contains(pts)) {
        auto* glFb = dynamic_cast<OpenGLFrameBuffer*>(frameBuffer.get());
        if (glFb) {
            auto texture = glFb->detachTexture();
            discard auto manager = texture->manager().lock();
            if (manager) {
                manager->restore(std::move(texture));
            }
        }
        discardPackets_.erase(pts);
        LogI("decode success, discard:{}", pts);
        return;
    }
    auto* framePtr = new AVFrame(AVFrameType::Video);
    auto frame = std::shared_ptr<AVFrame>(framePtr, [](AVFrame* frame) {
        if (auto* texRef = static_cast<std::shared_ptr<Texture>*>(frame->opaque)) {
            delete texRef;
            frame->opaque = nullptr;
        }
        delete frame;
    });
    frame->pts = pts;
    frame->timeScale = 1000000;
    us auto* glFb = dynamic_cast<OpenGLFrameBuffer*>(frameBuffer.get());
    TexturePtr rawTexture = glFb ? glFb->detachTexture() : nullptr;
    auto textureRef = new std::shared_ptr<Texture>(rawTexture.release(), [](Texture* tex) {
        if (tex) {
            Texture::releaseResource(TexturePtr(tex));
        }
    });
    frame->opaque = textureRef;
    auto videoFrameInfo = std::make_shared<VideoFrameInfo>();
    videoFrameInfo->width = videoConfig->width;
    videoFrameInfo->height = videoConfig->height;
    videoFrameInfo->format = FrameFormat::MediaCodecSurface;
    frame->info = std::move(videoFrameInfo);
    invokeReceiveFunc(std::move(frame));
}

void VideoHardwareDecoder::initResource() noexcept {
    if (resource_) {
        return;
    }
    resource_ = VideoDecodeResourceManager::shareInstance().find(config_->playerId);
    if (!resource_) {
        LogI("not found VideoDecodeResource");
        resource_ = std::make_shared<VideoDecodeResource>(config_->playerId);
        resource_->init();
    } else {
        VideoDecodeResourceManager::shareInstance().remove(config_->playerId);
    }
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    surface_ = resource_->createOffscreenSurface(videoConfig->width, videoConfig->height);
}

VideoHardwareDecoder::~VideoHardwareDecoder() noexcept {
    if (resource_) {
        if (surface_) {
            resource_->releaseSurface(std::move(surface_));
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
        resource_->attachContext(surface_.get());
    }
    decoderId_ = NativeHardwareDecoder::createVideoDecoder(videoConfig, mode_);
    if (decoderId_.empty()) {
        LogE("create video decoder failed!");
        return false;
    }
    NativeDecoderManager::shareInstance().add(decoderId_, shared_from_this());
    if (mode_ == DecodeMode::Texture) {
        resource_->detachContext();
    }
    isOpen_ = true;
    return true;
}

} // namespace slark
namespace slark
