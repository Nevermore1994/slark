//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "VideoDecodeResource.h"

#include <utility>

#include "GLContextManager.h"
#include "Log.hpp"

namespace slark {

VideoDecodeResource::VideoDecodeResource(std::string playerId) : playerId_(std::move(playerId)) {}

VideoDecodeResource::~VideoDecodeResource() noexcept {
    if (context_) {
        context_->release();
    }
}

void VideoDecodeResource::init() noexcept {
    auto context = GLContextManager::shareInstance().createShareContextWithId(playerId_);
    if (!context) {
        LogE("VideoDecodeResource: failed to create shared context for {}", playerId_);
        return;
    }
    context_ = std::dynamic_pointer_cast<AndroidEGLContext>(context);
    if (!context_) {
        LogE("VideoDecodeResource: shared context is not AndroidEGLContext for {}", playerId_);
        return;
    }
    frameBufferPool_ = std::make_unique<FrameBufferPool>();
}

void VideoDecodeResource::detachContext() noexcept {
    if (context_) {
        context_->detachContext();
    }
}

void VideoDecodeResource::attachContext(EGLSurface surface) noexcept {
    if (context_ && surface) {
        context_->attachContext(surface);
    }
}

FrameBufferRefPtr VideoDecodeResource::acquire(uint32_t width, uint32_t height) noexcept {
    if (frameBufferPool_) {
        return frameBufferPool_->acquire(width, height);
    }
    return nullptr;
}

EGLSurface VideoDecodeResource::createOffscreenSurface(uint32_t width, uint32_t height) noexcept {
    if (context_) {
        return context_->createOffscreenSurface(width, height);
    }
    return nullptr;
}

void VideoDecodeResource::releaseSurface(EGLSurface surface) noexcept {
    if (context_) {
        return context_->releaseSurface(surface);
    }
}

}  // namespace slark