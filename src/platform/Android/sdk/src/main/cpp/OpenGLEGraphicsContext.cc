//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "OpenGLEGraphicsContext.h"

#include "Log.hpp"

#include <utility>

namespace slark {

OpenGLEGraphicsContext::~OpenGLEGraphicsContext() noexcept {
    release();
}

bool OpenGLEGraphicsContext::isValid() const noexcept {
    return context_ != nullptr && frameBufferPool_ != nullptr;
}

bool OpenGLEGraphicsContext::init(void* sharedContext) noexcept {
    if (context_) {
        return true;
    }
    context_ = std::make_shared<AndroidEGLContext>();
    if (!context_->init(sharedContext)) {
        LogE("OpenGLEGraphicsContext init failed");
        context_.reset();
        return false;
    }
    frameBufferPool_ = std::make_unique<FrameBufferPool>();
    return true;
}

void OpenGLEGraphicsContext::release() noexcept {
    frameBufferPool_.reset();
    if (context_) {
        context_->release();
        context_.reset();
    }
}

void OpenGLEGraphicsContext::attachContext(IGraphicsSurface* surface) noexcept {
    if (!context_ || !surface) {
        LogE("attachContext: invalid context or surface");
        return;
    }
    auto* glSurface = dynamic_cast<OpenGLSurface*>(surface);
    if (!glSurface) {
        LogE("attachContext: surface is not OpenGLSurface");
        return;
    }
    context_->attachContext(glSurface->eglSurface());
}

void OpenGLEGraphicsContext::detachContext() noexcept {
    if (context_) {
        context_->detachContext();
    }
}

IGraphicsSurfaceRefPtr OpenGLEGraphicsContext::createOffscreenSurface(uint32_t width,
                                                                      uint32_t height) noexcept {
    if (!context_) {
        return nullptr;
    }
    auto eglSurface = context_->createOffscreenSurface(width, height);
    if (!eglSurface) {
        return nullptr;
    }
    return std::make_shared<OpenGLSurface>(eglSurface, context_->getDisplay());
}

void OpenGLEGraphicsContext::releaseSurface(IGraphicsSurfaceRefPtr surface) noexcept {
    if (!context_ || !surface) {
        return;
    }
    auto* glSurface = dynamic_cast<OpenGLSurface*>(surface.get());
    if (glSurface && glSurface->eglSurface()) {
        context_->releaseSurface(glSurface->eglSurface());
    }
}

IFrameBufferRefPtr OpenGLEGraphicsContext::acquireFrameBuffer(uint32_t width,
                                                              uint32_t height) noexcept {
    if (!frameBufferPool_) {
        return nullptr;
    }
    auto fb = frameBufferPool_->acquire(width, height);
    if (!fb) {
        return nullptr;
    }
    return std::make_shared<OpenGLFrameBuffer>(std::move(fb));
}

ITexturePtr OpenGLEGraphicsContext::createTexture(uint32_t width, uint32_t height) noexcept {
    auto texture = std::make_unique<Texture>(width, height);
    if (!texture->isValid()) {
        return nullptr;
    }
    return std::make_unique<OpenGLTexture>(TextureRefPtr(texture.release(), [](Texture* tex) {
        Texture::releaseResource(TexturePtr(tex));
    }));
}

void* OpenGLEGraphicsContext::nativeContext() noexcept {
    return context_ ? context_->nativeContext() : nullptr;
}

} // namespace slark
namespace slark
