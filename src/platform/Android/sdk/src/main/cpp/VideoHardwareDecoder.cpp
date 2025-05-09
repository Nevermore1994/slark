//
// Created by Nevermore on 2025/3/6.
//

#include "VideoHardwareDecoder.h"
#include "NativeHardwareDecoder.h"
#include "DecoderConfig.h"
#include "GLContextManager.h"
#include "Log.hpp"

namespace slark {

DecoderErrorCode VideoHardwareDecoder::decode(AVFrameRefPtr& frame) noexcept {
    assert(frame && frame->isVideo());
    auto pts = frame->pts;
    if (decodeFrames_.contains(pts)) {
        LogE("decode same pts frame, {}", pts);
        return DecoderErrorCode::InputDataError;
    }

    NativeDecodeFlag flag = NativeDecodeFlag::None;
    auto frameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frame->info);
    if (frameInfo->isIDRFrame) {
        flag = NativeDecodeFlag::KeyFrame;
    } else if (frameInfo->isEndOfStream){
        flag = NativeDecodeFlag::EndOfStream;
    }
    auto res = NativeHardwareDecoder::sendPacket(decoderId_, frame->data, pts, flag);
    if (res != DecoderErrorCode::Success) {
        LogE("send packet failed, {}", static_cast<int>(res));
        return res;
    }
    if (mode_ == DecodeMode::ByteBuffer) {
        //async
        decodeFrames_[pts] = std::move(frame);
    } else {
        //set fbo
        requestVideoFrame();
    }
    return res;
}

void VideoHardwareDecoder::requestVideoFrame() noexcept {
    if (!context_) {
        initContext();
    }
    if (!context_ || !frameBufferPool_) {
        LogE("context is null");
        return;
    }
    context_->attachContext(surface_);
    constexpr int64_t kWaitTime = 30; // 30ms
    constexpr int32_t kMaxRetryCount = 3;
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    auto frameBuffer = frameBufferPool_->fetch(videoConfig->width, videoConfig->height);
    glViewport(0, 0, static_cast<int>(videoConfig->width), static_cast<int>(videoConfig->height));
    frameBuffer->bind(true);
    int32_t retryCount = 0;
    bool isSuccess = false;
    uint64_t pts = 0;
    do {
        retryCount++;
        auto packed = NativeHardwareDecoder::requestVideoFrame(decoderId_, kWaitTime, videoConfig->width, videoConfig->height);
        isSuccess = (packed & (1ULL << 63)) != 0;
        pts = packed & 0x7FFFFFFFFFFFFFFF;
        if (!isSuccess) {
            LogE("request video frame failed:{}", retryCount);
        } else {
            LogI("request video frame success:{}", pts);
            break;
        }
    } while (!isSuccess && retryCount < kMaxRetryCount);
    auto frame = std::make_unique<AVFrame>(AVFrameType::Video);
    frame->pts = pts;
    auto texture = frameBuffer->detachTexture();
    frame->opaque = texture.release();
    auto videoFrameInfo = std::make_shared<VideoFrameInfo>();
    videoFrameInfo->width = videoConfig->width;
    videoFrameInfo->height = videoConfig->height;
    videoFrameInfo->format = FrameFormat::MediaCodecSurface;
    frame->info = std::move(videoFrameInfo);
    invokeReceiveFunc(std::move(frame));
}

void VideoHardwareDecoder::initContext() noexcept {
    if (context_) {
        return;
    }
    auto context = GLContextManager::shareInstance().createShareContextWithId(decoderId_);
    context_ = std::dynamic_pointer_cast<AndroidEGLContext>(context);
    if (!context_) {
        LogE();
    }
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    surface_ = context_->createOffscreenSurface(videoConfig->width, videoConfig->height);
}

VideoHardwareDecoder::~VideoHardwareDecoder() noexcept {
    if (context_) {
        if (surface_) {
            context_->releaseSurface(surface_);
        }
        context_->release();
        context_.reset();
    }
    NativeDecoderManager::shareInstance().remove(decoderId_);
    decoderId_.clear();
    frameBufferPool_ = std::make_unique<FrameBufferPool>();
}

bool VideoHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config);
    if (!videoConfig) {
        LogE("video decoder config is invalid");
        return false;
    }
    config_ = config;
    decoderId_ = NativeHardwareDecoder::createVideo(videoConfig);
    if (decoderId_.empty()) {
        LogE("create video decoder failed!");
        return false;
    }
    NativeDecoderManager::shareInstance().add(decoderId_, shared_from_this());
    LogI("create video decoder:{}", decoderId_);
    return true;
}

} // slark