//
// Created by Nevermore- on 2025/5/13.
//

#include "VideoRender.h"
#include "JNIEnvGuard.hpp"
#include "JNISignature.h"
#include "JNICache.h"
#include "JNIHelper.h"
#include "TexturePool.h"

namespace slark {

void Native_RequestRender(
    const std::string &playerId,
    GLuint textureId,
    uint32_t width,
    uint32_t height
) {
    using namespace slark::JNI;
    constexpr std::string_view kPlayerManagerClass = "com/slark/sdk/SlarkPlayerManager";
    JNIEnvGuard env(getJavaVM());
    auto jPlayer = JNI::ToJVM::toString(
        env.get(),
        playerId
    );
    auto playerClass = JNICache::shareInstance().getClass(
        env.get(),
        kPlayerManagerClass
    );
    if (!playerClass) {
        LogE("not found player manager class");
        return;
    }
    auto methodSignature = JNI::makeJNISignature(
        JNI::Void,
        JNI::String,
        JNI::Int,
        JNI::Int,
        JNI::Int
    );
    auto methodId = JNICache::shareInstance().getStaticMethodId(
        playerClass,
        "requestRender",
        methodSignature
    );
    if (!methodId) {
        LogE("not found method");
        return;
    }
    env.get()->CallStaticVoidMethod(
        playerClass.get(),
        methodId.get(),
        jPlayer.get(),
        static_cast<jint>(textureId),
        static_cast<jint>(width),
        static_cast<jint>(height)
    );

}

VideoRender::VideoRender()
    : renderThread_(
    std::make_unique<Thread>(
        "renderThread",
        &VideoRender::requestRenderFrame,
        this
    )
) {
    using namespace std::chrono_literals;
    renderThread_->setInterval(33ms); //30fps
}

VideoRender::~VideoRender() noexcept {
    if (renderThread_) {
        renderThread_->stop();
        renderThread_.reset();
    }
}

void VideoRender::start() noexcept {
    renderThread_->start();
    videoClock_.start();
}

void VideoRender::pause() noexcept {
    renderThread_->pause();
    videoClock_.pause();
}

void VideoRender::notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept {
    auto duration = videoInfo->frameDurationMs();
    LogE("frame duration: {}ms, fps:{}",
         duration,
         videoInfo->fps);
}

void VideoRender::notifyRenderInfo() noexcept {

}

void VideoRender::pushVideoFrameRender(AVFrameRefPtr frame) noexcept {
    renderFrame(frame);
}

void VideoRender::requestRenderFrame() noexcept {
    auto func = requestRenderFunc_.load();
    if (!func) {
        return;
    }
    auto frame = std::invoke(*func);
    renderFrame(frame);
}

void VideoRender::renderFrame(const AVFrameRefPtr &frame) noexcept {
    if (!frame) {
        return;
    }
    auto videoFrameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frame->info);
    if (videoFrameInfo->format == FrameFormat::MediaCodecSurface) {
        auto texture = std::unique_ptr<Texture>(static_cast<Texture *>(frame->opaque));
        if (texture == nullptr) {
            LogE("texture is nullptr");
            return;
        }
        frame->opaque = nullptr;
        auto textureId = texture->textureId();
        auto width = texture->width();
        auto height = texture->height();
        LogI("render video frame ptsTime:{}", frame->ptsTime());
        {
            std::lock_guard lock(mutex_);
            renderInfos_.try_emplace(
                textureId,
                RenderInfo(frame->ptsTime(), std::move(texture))
            );
        }
        Native_RequestRender(
            playerId_,
            textureId,
            width,
            height
        );
    } else {
        LogE("not support frame format:{}",
             static_cast<int>(videoFrameInfo->format));
    }
}

void VideoRender::renderFrameComplete(int32_t id) noexcept {
    std::lock_guard lock(mutex_);
    auto textureId = static_cast<uint32_t>(id);
    if (renderInfos_.contains(textureId)) {
        auto info = std::move(renderInfos_[textureId]);
        renderInfos_.erase(textureId);
        Texture::releaseResource(std::move(info.texture));
        videoClock_.setTime(Time::TimePoint::fromSeconds(info.ptsTime));
        LogI("render complete, ptsTime:{}, texture id:{}", info.ptsTime, id);
    } else {
        LogE("not found texture id:{}", id);
    }
}

void VideoRender::renderEnd() noexcept {
    renderThread_->pause();
}

} // slark