//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>
#include <memory>

namespace slark {

enum class GraphicsBackendType : int32_t {
    OpenGL = 0,
    Vulkan = 1,
};

class ITexture {
public:
    virtual ~ITexture() = default;

    virtual uint32_t width() const noexcept = 0;
    virtual uint32_t height() const noexcept = 0;
    virtual bool isValid() const noexcept = 0;
    virtual void bind() const noexcept = 0;
    virtual void unbind() const noexcept = 0;
};

using ITexturePtr = std::unique_ptr<ITexture>;
using ITextureRefPtr = std::shared_ptr<ITexture>;

class IFrameBuffer {
public:
    virtual ~IFrameBuffer() = default;

    virtual bool bind(bool isRebinding = false) noexcept = 0;
    virtual void unbind(bool isRollback = false) noexcept = 0;
    virtual bool isValid() const noexcept = 0;
    virtual uint32_t width() const noexcept = 0;
    virtual uint32_t height() const noexcept = 0;
    virtual ITextureRefPtr texture() const noexcept = 0;
};

using IFrameBufferRefPtr = std::shared_ptr<IFrameBuffer>;

class IGraphicsSurface {
public:
    virtual ~IGraphicsSurface() = default;

    virtual void* nativeSurface() const noexcept = 0;
    virtual bool isValid() const noexcept = 0;
};

using IGraphicsSurfacePtr = std::unique_ptr<IGraphicsSurface>;
using IGraphicsSurfaceRefPtr = std::shared_ptr<IGraphicsSurface>;

class IGraphicsContext {
public:
    virtual ~IGraphicsContext() = default;

    virtual GraphicsBackendType backend() const noexcept = 0;
    virtual bool isValid() const noexcept = 0;
    virtual bool init(void* sharedContext) noexcept = 0;
    virtual void release() noexcept = 0;
    virtual void attachContext(IGraphicsSurface* surface) noexcept = 0;
    virtual void detachContext() noexcept = 0;

    virtual IGraphicsSurfaceRefPtr createOffscreenSurface(uint32_t width,
                                                          uint32_t height) noexcept = 0;
    virtual void releaseSurface(IGraphicsSurfaceRefPtr surface) noexcept = 0;

    virtual IFrameBufferRefPtr acquireFrameBuffer(uint32_t width, uint32_t height) noexcept = 0;
    virtual ITextureRefPtr createTexture(uint32_t width, uint32_t height) noexcept = 0;

    virtual void* nativeContext() noexcept = 0;
};

using IGraphicsContextPtr = std::unique_ptr<IGraphicsContext>;
using IGraphicsContextRefPtr = std::shared_ptr<IGraphicsContext>;

} // namespace slark
