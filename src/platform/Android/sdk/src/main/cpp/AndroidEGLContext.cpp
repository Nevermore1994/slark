//
// Created by Nevermore on 2025/2/22.
//

#include "AndroidEGLContext.h"
#include "Log.hpp"
#include <cassert>

namespace slark {

AndroidEGLContext::AndroidEGLContext() = default;

AndroidEGLContext::~AndroidEGLContext() noexcept {
    destroy();
}

bool AndroidEGLContext::init(void* context) noexcept {
    if (isInit_) {
        return true;
    }
    assert(display_ == EGL_NO_DISPLAY);
    if (display_ != EGL_NO_DISPLAY) {
        LogI("EGL already set up");
        return false;
    }
    if (context == nullptr) {
        context = EGL_NO_CONTEXT;
    }

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(display_ != EGL_NO_DISPLAY);

    if (!eglInitialize(display_, nullptr, nullptr)) {
        display_ = EGL_NO_DISPLAY;
        LogE("unable to initialize EGL");
        return false;
    }

    auto config = getConfig(2);
    int openGLESAttrib2[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    context_ = eglCreateContext(display_, config,context, openGLESAttrib2);
    if (eglGetError() == EGL_SUCCESS) {
        LogI("create opengl es 2.0 success");
        isInit_ = true;
        config_ = config;
        return true;
    }
    isInit_ = false;
    return false;
}

void AndroidEGLContext::release() noexcept {
    destroy();
}

void AndroidEGLContext::attachContext(EGLSurface surface) noexcept {
    attachContext(surface, surface);
}

void AndroidEGLContext::attachContext(EGLSurface drawSurface, EGLSurface readSurface) noexcept {
    if (!isInit_) {
        LogE("gl context is uninitialized");
        return;
    }
    if (display_ == EGL_NO_DISPLAY) {
        LogE("not set display");
    }
    if (!eglMakeCurrent(display_, drawSurface, readSurface, context_)) {
        LogE("attach context error");
    }
}


void AndroidEGLContext::detachContext() noexcept {
    if (!isInit_) {
        LogE("gl context is uninitialized");
        return;
    }
    if (display_ == EGL_NO_DISPLAY) {
        LogE("not set display");
    }
    if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        LogE("detach context error");
    }
}

void AndroidEGLContext::releaseSurface(EGLSurface eglSurface) noexcept {
    eglDestroySurface(display_, eglSurface);
}

EGLSurface AndroidEGLContext::createWindowSurface(ANativeWindow* surface) noexcept {
    if (surface == nullptr) {
        LogE("ANativeWindow is NULL!");
        return nullptr;
    }
    int surfaceAttributes[] = {
            EGL_NONE
    };

    EGLSurface eglSurface = eglCreateWindowSurface(display_, config_, surface, surfaceAttributes);
    if (eglSurface == nullptr) {
        LogE("EGLSurface is NULL!");
    }
    return eglSurface;
}

EGLSurface AndroidEGLContext::createOffscreenSurface(uint32_t width, uint32_t height) noexcept {
    int surfaceAttributes[] = {
            EGL_WIDTH, static_cast<int>(width),
            EGL_HEIGHT, static_cast<int>(height),
            EGL_NONE
    };
    EGLSurface eglSurface = eglCreatePbufferSurface(display_, config_, surfaceAttributes);
    if (eglSurface == nullptr) {
        LogE("Surface was null");
        return nullptr;
    }
    return eglSurface;
}

EGLConfig AndroidEGLContext::getConfig(int version) noexcept {
    int renderableType = EGL_OPENGL_ES2_BIT;
    int attribList[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_STENCIL_SIZE, 8,
            EGL_RENDERABLE_TYPE, renderableType,
            EGL_NONE,
    };
    EGLConfig configs = nullptr;
    int numConfigs;
    if (!eglChooseConfig(display_, attribList, &configs, 1, &numConfigs)) {
        return nullptr;
    }
    return configs;
}

void* AndroidEGLContext::nativeContext() noexcept {
    return context_;
}

void AndroidEGLContext::destroy() noexcept {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display_, context_);
        eglReleaseThread();
        eglTerminate(display_);
    }

    display_ = EGL_NO_DISPLAY;
    context_ = EGL_NO_CONTEXT;
    config_ = nullptr;
}

IEGLContextRefPtr createGLContext() noexcept {
    return std::make_shared<AndroidEGLContext>();
}

IEGLContextRefPtr createMainGLContext() noexcept {
    auto context = createGLContext();
    context->init(nullptr);
    return context;
}

} // slark