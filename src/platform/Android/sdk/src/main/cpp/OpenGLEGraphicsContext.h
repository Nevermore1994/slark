//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AndroidEGLContext.h"
#include "FrameBufferPool.h"
#include "IGraphicsContext.h"
#include "OpenGLFrameBuffer.h"
#include "OpenGLSurface.h"
#include "OpenGLTexture.h"

namespace slark {

// OpenGL graphics context implementing IGraphicsContext
class OpenGLEGraphicsContext : public IGraphicsContext {
 public:
    OpenGLEGraphicsContext() = default;
    ~OpenGLEGraphicsContext() noexcept override;

    GraphicsBackendType backend() const noexcept override {
        return GraphicsBackendType::OpenGL;
    }

    bool isValid() const noexcept override;
    bool init(void* sharedContext) noexcept override;
    void release() noexcept override;
    void attachContext(IGraphicsSurface* surface) noexcept override;
    void detachContext() noexcept override;

    IGraphicsSurfaceRefPtr createOffscreenSurface(uint32_t width,
                                                  uint32_t height) noexcept override;
    void releaseSurface(IGraphicsSurfaceRefPtr surface) noexcept override;

    IFrameBufferRefPtr acquireFrameBuffer(uint32_t width, uint32_t height) noexcept override;
    ITextureRefPtr createTexture(uint32_t width, uint32_t height) noexcept override;

    void* nativeContext() noexcept override;

    // 兼容旧接口，直接获取 AndroidEGLContext
    AndroidEGLContext* eglContext() const noexcept {
        return context_.get();
    }

 private:
    std::shared_ptr<AndroidEGLContext> context_;
    std::unique_ptr<FrameBufferPool> frameBufferPool_;
};

}  // namespace slark
