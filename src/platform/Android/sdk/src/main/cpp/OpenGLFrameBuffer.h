//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "FrameBuffer.h"
#include "IGraphicsContext.h"
#include "OpenGLTexture.h"

namespace slark {

// OpenGL frame buffer implementing IFrameBuffer interface,
// wraps existing FrameBuffer
class OpenGLFrameBuffer : public IFrameBuffer {
public:
    explicit OpenGLFrameBuffer(FrameBufferRefPtr frameBuffer)
        : frameBuffer_(std::move(frameBuffer)) {}

    bool bind(bool isRebinding = false) noexcept override {
        return frameBuffer_->bind(isRebinding);
    }

    void unbind(bool isRollback = false) noexcept override {
        frameBuffer_->unbind(isRollback);
    }

    bool isValid() const noexcept override {
        return frameBuffer_->isValid();
    }

    uint32_t width() const noexcept override {
        return frameBuffer_->width();
    }

    uint32_t height() const noexcept override {
        return frameBuffer_->height();
    }

    ITextureRefPtr texture() const noexcept override {
        auto& tex = frameBuffer_->texture();
        if (!tex) {
            return nullptr;
        }
        // Wrap raw Texture* in OpenGLTexture (non-owning for pool texture lifecycle)
        return std::make_shared<OpenGLTexture>(TextureRefPtr(tex.get(), [](Texture*) {
        }));
    }

    GLuint glFrameBufferId() const noexcept {
        return frameBuffer_->frameBufferId();
    }

    const FrameBufferRefPtr& innerFrameBuffer() const noexcept {
        return frameBuffer_;
    }

    TexturePtr detachTexture() noexcept {
        return frameBuffer_->detachTexture();
    }

private:
    FrameBufferRefPtr frameBuffer_;
};

} // namespace slark
