//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "IGraphicsContext.h"

#include <EGL/egl.h>

namespace slark {

/ OpenGL surface implementing IGraphicsSurface,
    wraps EGLSurface class OpenGLSurface : public IGraphicsSurface {
public:
    OpenGLSurface(EGLSurface surface, EGLDisplay display) : surface_(surface), display_(display) {}

    ~OpenGLSurface() override = default;

    void* nativeSurface() const noexcept override {
        return reinterpret_cast<void*>(surface_);
    }

    bool isValid() const noexcept override {
        return surface_ != EGL_NO_SURFACE;
    }

    EGLSurface eglSurface() const noexcept {
        return surface_;
    }

private:
    EGLSurface surface_;
    EGLDisplay display_;
};

} // namespace slark
namespace slark
