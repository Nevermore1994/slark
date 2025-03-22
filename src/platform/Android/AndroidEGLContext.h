//
// Created by Nevermore on 2025/2/22.
//

#pragma once
#include "IEGLContext.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>

namespace slark {

class AndroidEGLContext : public IEGLContext {
public:
    AndroidEGLContext();

    ~AndroidEGLContext() override;

    bool init(void* context) override;

    void release() override;

    void attachContext() override;

    void detachContext() override;

    void attachContext(EGLSurface surface);

    void attachContext(EGLSurface drawSurface, EGLSurface readSurface);

    void* nativeContext() override;

    void releaseSurface(EGLSurface eglSurface);

    EGLSurface createWindowSurface(ANativeWindow* surface);

    EGLSurface createOffscreenSurface(int width, int height);
private:
    EGLConfig getConfig(int version);

    void destroy();
private:
    bool isInit_ = false;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLDisplay display_ = EGL_NO_DISPLAY;
};

} // slark

