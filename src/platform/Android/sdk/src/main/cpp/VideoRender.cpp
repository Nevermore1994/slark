//
// Created by Nevermore- on 2025/5/13.
//

#include "VideoRender.h"

#include "JNICache.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"
#include "JNISignature.h"
#include "Log.hpp"
#include "TexturePool.h"

namespace slark {

void Native_RequestRender(const std::string& playerId,
                          GLuint textureId,
                          uint32_t width,
                          uint32_t height) {
    using namespace slark::JNI;
    constexpr std::string_view kPlayerManagerClass = "com/slark/sdk/SlarkPlayerManager";
    JNIEnvGuard env(getJavaVM());
    auto jPlayer = JNI::ToJVM::str(env.get(), playerId);
    auto playerClass = JNICache::instance().getClass(env.get(), kPlayerManagerClass);
    if (!playerClass) {
        LogE("not found player manager class");
        return;
    }
    auto methodSignature = JNI::makeSignature(JNI::Sig::Void,
                                              JNI::Sig::String,
                                              JNI::Sig::Int,
                                              JNI::Sig::Int,
                                              JNI::Sig::Int);
    auto methodId =
        JNICache::instance().getStaticMethod(playerClass, "requestRender", methodSignature);
    if (!methodId) {
        LogE("not found method");
        return;
    }
    env.get()->CallStaticVoidMethod(playerClass.get(),
                                    methodId.get(),
                                    jPlayer.get(),
                                    static_cast<jint>(textureId),
                                    static_cast<jint>(width),
                                    static_cast<jint>(height));
}

VideoRender::VideoRender() = default;

VideoRender::~VideoRender() noexcept = default;

void VideoRender::start() noexcept {
    videoClock_.start();
    LogI("VideoRender started, playerId:{}", playerId_);
}

void VideoRender::pause() noexcept {
    videoClock_.pause();
    LogI("VideoRender paused, playerId:{}", playerId_);
}

void VideoRender::notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept {
    LogI("video info: fps:{}, frame duration:{}ms, rotation:{}",
         videoInfo->fps,
         videoInfo->frameDurationMs().count(),
         videoInfo->rotationDegrees);
    rotationDegrees_ = videoInfo->rotationDegrees;
}

void VideoRender::onVSync() noexcept {
    requestRenderFrame();
}

void VideoRender::notifyRenderInfo() noexcept {}

void VideoRender::pushFrame(AVFrameRefPtr frame) noexcept {
    renderFrame(frame);
}

void VideoRender::requestRenderFrame() noexcept {
    auto func = requestRenderFunc_.load();
    if (!func) {
        return;
    }
    auto frame = std::invoke(*func);
    if (!frame) {
        LogD("requestRenderFrame: no frame available");
        return;
    }
    renderFrame(frame);
}

void Native_SetVideoRotation(const std::string& playerId, int32_t rotationDegrees) {
    using namespace slark::JNI;
    constexpr std::string_view kClass = "com/slark/sdk/SlarkPlayerManager";
    JNIEnvGuard env(getJavaVM());
    auto playerClass = JNICache::instance().getClass(env.get(), kClass);
    if (!playerClass) {
        LogE("not found player manager class");
        return;
    }
    auto methodSig = JNI::makeSignature(JNI::Sig::Void, JNI::Sig::String, JNI::Sig::Int);
    auto methodId = JNICache::instance().getStaticMethod(playerClass, "setVideoRotation", methodSig);
    if (!methodId) {
        LogE("not found setVideoRotation method");
        return;
    }
    env.get()->CallStaticVoidMethod(playerClass.get(), methodId.get(),
                                    JNI::ToJVM::str(env.get(), playerId).get(),
                                    static_cast<jint>(rotationDegrees));
}

void VideoRender::renderFrame(const AVFrameRefPtr& frame) noexcept {
    if (!frame) {
        return;
    }
    auto videoFrameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frame->info);
    if (!videoFrameInfo) {
        LogE("videoFrameInfo is null");
        return;
    }
    if (!rotationApplied_ && rotationDegrees_ != 0) {
        Native_SetVideoRotation(playerId_, rotationDegrees_);
        rotationApplied_ = true;
    }
    if (videoFrameInfo->format == FrameFormat::MediaCodecSurface) {
        auto* tex = static_cast<Texture*>(frame->opaque);
        if (!tex) {
            LogE("texture is nullptr");
            return;
        }
        frame->opaque = nullptr;
        TextureRefPtr texture(tex, [](Texture* t) {
            Texture::releaseResource(TexturePtr(t));
        });
        auto textureId = texture->textureId();
        auto width = texture->width();
        auto height = texture->height();
        LogI("render video frame ptsTime:{}", frame->ptsTime());
        {
            std::lock_guard lock(mutex_);
            renderInfos_.try_emplace(textureId, RenderInfo(frame->ptsTime(), std::move(texture)));
        }
        Native_RequestRender(playerId_, textureId, width, height);
    } else {
        LogE("not support frame format:{}", static_cast<int>(videoFrameInfo->format));
    }
}

void VideoRender::renderFrameComplete(int32_t id) noexcept {
    std::lock_guard lock(mutex_);
    auto textureId = static_cast<uint32_t>(id);
    if (renderInfos_.contains(textureId)) {
        auto info = std::move(renderInfos_[textureId]);
        renderInfos_.erase(textureId);
        auto ptsTime = info.ptsTime;
        videoClock_.setTime(Time::TimePoint::fromSeconds(ptsTime));
        // 同步更新 ClockSync::videoTime_，确保 AV Sync 正常工作
        if (videoTimeUpdater_) {
            videoTimeUpdater_(Time::TimePoint::fromSeconds(ptsTime));
        }
        backupRenderInfo_ = std::move(info);
        LogI("rendered video, ptsTime:{}, texture id:{}", ptsTime, id);
    } else {
        LogE("not found texture id:{}", id);
    }
}

void VideoRender::renderEnd() noexcept {
    videoClock_.pause();
    LogI("VideoRender renderEnd, playerId:{}", playerId_);
}

} // namespace slark