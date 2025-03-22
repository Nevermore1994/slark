//
// Created by Nevermore on 2025/2/22.
//

#include "AndroidEGLContext.h"
#include "Log.hpp"
#include <cassert>

namespace slark {

AndroidEGLContext::AndroidEGLContext() = default;

AndroidEGLContext::~AndroidEGLContext() {
    destroy();
}

bool AndroidEGLContext::init(void* context) {
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

    EGLConfig config = getConfig(3);
    int openGLESAttrib3[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
    };
    context_ = eglCreateContext(display_, config,context, openGLESAttrib3);
    if (eglGetError() == EGL_SUCCESS) {
        LogI("create opengl es 3.0 success");
        isInit_ = true;
        config_ = config;
        return true;
    }

    config = getConfig(2);
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

void AndroidEGLContext::release() {
    destroy();
}

void AndroidEGLContext::attachContext() {
    attachContext(EGL_NO_SURFACE);
}

void AndroidEGLContext::attachContext(EGLSurface surface) {
    attachContext(surface, surface);
}

void AndroidEGLContext::attachContext(EGLSurface drawSurface, EGLSurface readSurface) {
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


void AndroidEGLContext::detachContext() {
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

void AndroidEGLContext::releaseSurface(EGLSurface eglSurface) {
    eglDestroySurface(display_, eglSurface);
}

EGLSurface AndroidEGLContext::createWindowSurface(ANativeWindow* surface) {
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

EGLSurface AndroidEGLContext::createOffscreenSurface(int width, int height) {
    int surfaceAttributes[] = {
            EGL_WIDTH, width,
            EGL_HEIGHT, height,
            EGL_NONE
    };
    EGLSurface eglSurface = eglCreatePbufferSurface(display_, config_, surfaceAttributes);
    if (eglSurface == nullptr) {
        LogE("Surface was null");
        return nullptr;
    }
    return eglSurface;
}

EGLConfig AndroidEGLContext::getConfig(int version) {
    int renderableType = EGL_OPENGL_ES2_BIT;
    if (version >= 3) {
        renderableType = EGL_OPENGL_ES3_BIT;
    }
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

void* AndroidEGLContext::nativeContext() {
    return context_;
}

void AndroidEGLContext::destroy() {
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

IEGLContextPtr createEGLContext() {
    return std::make_unique<AndroidEGLContext>();
}

} // slark