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
    auto inputPts = int64_t(frame->ptsTime() * 1000000); // convert to microseconds
    auto res = NativeHardwareDecoder::sendPacket(
        decoderId_,
        frame->data,
        inputPts,
        flag
    );
    if (res < DecoderErrorCode::Unknown) {
        LogE("send video packet failed, {}",
             static_cast<int>(res));
    } else {
        LogI("send video packet success:{}, pts:{}, dts:{}, isDiscard:{}",
             static_cast<int>(res),
             frame->ptsTime(),
             frame->dtsTime(),
             frame->isDiscard
        );
        if (frame->isDiscard) {
            discardPackets_.insert(inputPts);
        }
    }
    return res;
}

DecoderErrorCode VideoHardwareDecoder::decodeByteBufferMode(
    AVFrameRefPtr &frame
) noexcept {
    auto pts = frame->pts;
    auto res = sendPacket(frame);
    if (res < DecoderErrorCode::Unknown) {
        LogE("send packet failed, {}", static_cast<int>(res));
    } else {
        LogI("send video packet success, pts:{}, dts:{}",
             pts,
             frame->dts
        );
    }
    return res;
}

void VideoHardwareDecoder::receiveDecodedData(
    DataPtr data,
    int64_t pts,
    bool isCompleted
) noexcept {
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
    if (res == DecoderErrorCode::Success) {
        requestDecodeVideoFrame();
    }
    context_->detachContext();
    return res;
}

void VideoHardwareDecoder::flush() noexcept {
    NativeDecoder::flush();
    discardPackets_.clear();
    isCompleted_ = false;
}

void VideoHardwareDecoder::requestDecodeVideoFrame() noexcept {
    constexpr int64_t kWaitTime = 3; // 10ms
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
    bool isCompleted = false;
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
        isCompleted = (packed & (1ULL << 62)) != 0;
        pts = static_cast<int64_t>(packed) & 0x3FFFFFFFFFFFFFFF;
        if (!isSuccess) {
            LogE("decode video frame failed:{}",
                 retryCount);
        } else {
            LogI("decode video frame success:{}, {}",
                 pts,
                 frameBuffer->texture()->textureId());
            break;
        }
    } while (retryCount < kMaxRetryCount);
    frameBuffer->unbind(true);
    isCompleted_ = isCompleted;
    if (isCompleted) {
        LogI("decode video completed");
    }
    if (!isSuccess) {
        LogE("request decode video frame failed after {} retries",
             retryCount);
        return;
    }
    if (discardPackets_.contains(pts)) {
        auto texture = frameBuffer->detachTexture(); //discard
        auto manager = texture->manager().lock();
        if (manager) {
            manager->restore(std::move(texture));
        }
        discardPackets_.erase(pts);
        LogI("decode success, discard:{}", pts);
        return;
    }
    auto* framePtr = new AVFrame(AVFrameType::Video);
    auto frame = std::shared_ptr<AVFrame>(framePtr, [](AVFrame* frame) {
        if (auto videoFrameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frame->info);
            videoFrameInfo && videoFrameInfo->format == FrameFormat::MediaCodecSurface && frame->opaque) {
            Texture::releaseResource(TexturePtr(static_cast<Texture*>(frame->opaque)));
            frame->opaque = nullptr;
        }
        delete frame;
    });
    frame->pts = pts;
    frame->timeScale = 1000000; //us
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
    LogI("create context start");
    auto context = GLContextManager::shareInstance().createShareContextWithId(config_->playerId);
    context_ = std::dynamic_pointer_cast<AndroidEGLContext>(context);
    if (!context_) {
        LogE("create context, not AndroidEGLContext");
        return;
    }
    LogI("create context success");
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    surface_ = context_->createOffscreenSurface(
        videoConfig->width,
        videoConfig->height
    );
    frameBufferPool_ = std::make_unique<FrameBufferPool>();
    LogI("create surface & frameBuffer success");
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
    auto startTime = Time::nowTimeStamp();
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
    LogI("create video decoder start");
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
    LogI("create video decoder end:{}, cost time:{}", decoderId_, (Time::nowTimeStamp() - startTime).toMilliSeconds());
    if (mode_ == DecodeMode::Texture) {
        context_->detachContext();
    }
    isOpen_ = true;
    return true;
}

} // slark