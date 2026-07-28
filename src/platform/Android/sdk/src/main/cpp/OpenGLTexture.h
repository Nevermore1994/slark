//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "IGraphicsContext.h"
#include "Texture.h"

namespace slark {

// OpenGL texture implementing ITexture interface,
// wraps existing Texture
class OpenGLTexture : public ITexture {
 public:
    explicit OpenGLTexture(TextureRefPtr texture) : texture_(std::move(texture)) {}

    uint32_t width() const noexcept override {
        return texture_->width();
    }

    uint32_t height() const noexcept override {
        return texture_->height();
    }

    bool isValid() const noexcept override {
        return texture_->isValid();
    }

    void bind() const noexcept override {
        texture_->bind();
    }

    void unbind() const noexcept override {
        texture_->unbind();
    }

    GLuint glTextureId() const noexcept {
        return texture_->textureId();
    }

    const TextureRefPtr& innerTexture() const noexcept {
        return texture_;
    }

 private:
    TextureRefPtr texture_;
};

}  // namespace slark
