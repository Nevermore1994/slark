//
// Created by Nevermore on 2025/3/6.
//

#include "VideoHardwareDecoder.h"
#include "NativeHardwareDecoder.h"
#include "DecoderConfig.h"
#include "GLContextManager.h"
#include "Log.hpp"

namespace slark {

DecoderErrorCode VideoHardwareDecoder::decode(
    AVFrameRefPtr &frame
) noexcept {
    assert(frame && frame->isVideo());
    if (!isOpen_) {
        LogE("decoder is not open");
        return DecoderErrorCode::NotStart;
    }
    auto isByteBufferMode = mode_ == DecodeMode::ByteBuffer;
    if (isByteBufferMode) {
        return decodeByteBufferMode(frame);
    } else {
        return decodeTextureMode(frame);
    }
}

DecoderErrorCode VideoHardwareDecoder::sendPacket(
    AVFrameRefPtr &frame
) noexcept {
    NativeDecodeFlag flag = NativeDecodeFlag::None;
    auto frameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frame->info);
    if (frameInfo->isIDRFrame) {
        flag = NativeDecodeFlag::KeyFrame;
    } else if (frameInfo->isEndOfStream) {
        flag = NativeDecodeFlag::EndOfStream;
    }
    auto res = NativeHardwareDecoder::sendPacket(
        decoderId_,
        frame->data,
        int64_t(frame->ptsTime() * 1000000), // convert to microseconds
        flag
    );
    if (res != DecoderErrorCode::Success) {
        LogE("send video packet failed, {}",
             static_cast<int>(res));
    } else {
        LogI("send video packet success, pts:{}, dts:{}",
             frame->pts,
             frame->dts
        );
    }
    return res;
}

DecoderErrorCode VideoHardwareDecoder::decodeByteBufferMode(
    AVFrameRefPtr &frame
) noexcept {
    auto pts = frame->pts;
    auto res = sendPacket(frame);
    if (res != DecoderErrorCode::Success) {
        LogE("send packet failed, {}", static_cast<int>(res));
    } else {
        LogI("send video packet success, pts:{}, dts:{}",
             pts,
             frame->dts
        );
    }
    return res;
}

void VideoHardwareDecoder::decodeComplete(
    DataPtr data,
    int64_t pts
) noexcept {
    if (!data || data->empty()) {
        LogE("data is empty");
        return;
    }
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    auto frame = std::make_unique<AVFrame>(AVFrameType::Video);
    frame->pts = pts;
    frame->timeScale = 1000000; //us
    frame->data = std::move(data);
    auto videoFrameInfo = std::make_shared<VideoFrameInfo>();
    videoFrameInfo->format = FrameFormat::MediaCodecByteBuffer;
    videoFrameInfo->width = videoConfig->width;
    videoFrameInfo->height = videoConfig->height;
    frame->info = std::move(videoFrameInfo);
    invokeReceiveFunc(std::move(frame));
}

DecoderErrorCode VideoHardwareDecoder::decodeTextureMode(
    AVFrameRefPtr &frame
) noexcept {
    if (!context_ || !frameBufferPool_) {
        LogE("context is null");
        return DecoderErrorCode::NotProcessed;
    }
    context_->attachContext(surface_);
    auto res = sendPacket(frame);
    requestVideoFrame();
    context_->detachContext();
    return res;
}

void VideoHardwareDecoder::requestVideoFrame() noexcept {
    constexpr int64_t kWaitTime = 10; // 10ms
    constexpr int32_t kMaxRetryCount = 3;
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    auto frameBuffer = frameBufferPool_->acquire(
        videoConfig->width,
        videoConfig->height
    );
    glViewport(
        0,
        0,
        static_cast<int>(videoConfig->width),
        static_cast<int>(videoConfig->height)
    );
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
    int64_t pts = 0;
    do {
        retryCount++;
        auto packed = NativeHardwareDecoder::requestVideoFrame(
            decoderId_,
            kWaitTime,
            videoConfig->width,
            videoConfig->height
        );
        isSuccess = (packed & (1ULL << 63)) != 0;
        pts = static_cast<int64_t>(packed) & 0x7FFFFFFFFFFFFFFF;
        if (!isSuccess) {
            LogE("request video frame failed:{}",
                 retryCount);
        } else {
            LogI("request video frame success:{}",
                 pts);
            break;
        }
    } while (!isSuccess && retryCount < kMaxRetryCount);
    if (!isSuccess) {
        LogE("request video frame failed after {} retries",
             retryCount);
        return;
    }
    auto frame = std::make_unique<AVFrame>(AVFrameType::Video);
    frame->pts = pts;
    frame->timeScale = 1000; //ms
    auto texture = frameBuffer->detachTexture();
    frame->opaque = texture.release();
    frameBuffer->unbind(true);
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
    auto context = GLContextManager::shareInstance().createShareContextWithId(config_->playerId);
    context_ = std::dynamic_pointer_cast<AndroidEGLContext>(context);
    if (!context_) {
        LogE("create context, not AndroidEGLContext");
        return;
    }
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    surface_ = context_->createOffscreenSurface(
        videoConfig->width,
        videoConfig->height
    );
    frameBufferPool_ = std::make_unique<FrameBufferPool>();
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
    frameBufferPool_.reset();
}

bool VideoHardwareDecoder::open(
    std::shared_ptr<DecoderConfig> config
) noexcept {
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config);
    if (!videoConfig) {
        LogE("video decoder config is invalid");
        return false;
    }
    config_ = config;
    if (mode_ == DecodeMode::Texture) {
        initContext();
        if (!context_) {
            LogE("init context failed");
            return false;
        }
        if (!frameBufferPool_) {
            LogE("create frame buffer pool failed");
            return false;
        }
        context_->attachContext(surface_);
    }
    decoderId_ = NativeHardwareDecoder::createVideoDecoder(
        videoConfig,
        mode_
    );
    if (decoderId_.empty()) {
        LogE("create video decoder failed!");
        return false;
    }
    NativeDecoderManager::shareInstance().add(
        decoderId_,
        shared_from_this()
    );
    LogI("create video decoder:{}",
         decoderId_);
    if (mode_ == DecodeMode::Texture) {
        context_->detachContext();
    }
    isOpen_ = true;
    return true;
}

} // slark