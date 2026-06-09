//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once
#include <cstdint>
#include <memory>

#if defined(__ANDROID__)
#include <EGL/egl.h>
#endif

namespace slark {

class IEGLContext {
public:
    virtual ~IEGLContext() = default;

    virtual bool init(void* context) = 0;

    virtual void release() = 0;

#if SLARK_IOS
    virtual void attachContext() = 0;
#elif defined(__ANDROID__)
    virtual void attachContext(EGLSurface surface) = 0;

    virtual void attachContext(EGLSurface drawSurface, EGLSurface readSurface) = 0;
#endif
    virtual void detachContext() = 0;

    virtual void* nativeContext() = 0;
};

using IEGLContextPtr = std::unique_ptr<IEGLContext>;
using IEGLContextRefPtr = std::shared_ptr<IEGLContext>;

IEGLContextRefPtr createGLContext() noexcept;
IEGLContextRefPtr createMainGLContext() noexcept;

} // namespace slark
