//
// Created by Nevermore on 2025/2/22.
//

#pragma once
#include "IEGLContext.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>

namespace slark {

class AndroidEGLContext : public IEGLContext {
public:
    AndroidEGLContext();

    ~AndroidEGLContext() noexcept override;

    bool init(void* context) noexcept override;

    void release() noexcept override;

    void detachContext() noexcept override;

    void attachContext(EGLSurface surface) noexcept override;

    void attachContext(EGLSurface drawSurface, EGLSurface readSurface) noexcept override;

    void* nativeContext() noexcept override;

    void releaseSurface(EGLSurface eglSurface) noexcept;

    EGLSurface createWindowSurface(ANativeWindow* surface) noexcept;

    EGLSurface createOffscreenSurface(uint32_t width, uint32_t height) noexcept;
private:
    EGLConfig getConfig(int version) noexcept;

    void destroy() noexcept;
private:
    bool isInit_ = false;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLDisplay display_ = EGL_NO_DISPLAY;
};

} // slark

