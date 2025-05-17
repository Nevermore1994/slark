//
// Created by Nevermore- on 2025/5/6.
//

#pragma once
#include "BitMap.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <vector>
#include <mutex>

namespace slark {

class TexturePool;

struct TextureConfig {
    struct Warp {
        GLint s = GL_CLAMP_TO_EDGE;
        GLint t = GL_CLAMP_TO_EDGE;
    };
    struct Filter {
        GLint min = GL_LINEAR;
        GLint mag = GL_LINEAR;
    };
    Warp warp{};
    Filter filter{};
    GLint internalFormat = GL_RGBA;
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
};

class Texture {
public:
    Texture(uint32_t width, uint32_t height, TextureConfig config = {}, DataPtr data = nullptr);

    ~Texture() {
        if (textureId_) {
            glDeleteTextures(1, &textureId_);
            textureId_ = 0;
        }
    }

    void updateData(DataPtr data) const noexcept;

    [[nodiscard]] uint32_t width() const noexcept {
        return width_;
    }

    [[nodiscard]] uint32_t height() const noexcept {
        return height_;
    }

    [[nodiscard]] GLuint textureId() const noexcept {
        return textureId_;
    }

    [[nodiscard]] const TextureConfig &config() const noexcept {
        return config_;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return textureId_ != 0;
    }

    void bind() const noexcept {
        glBindTexture(GL_TEXTURE_2D, textureId_);
    }

    void unbind() const noexcept {
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void setManager(std::weak_ptr<TexturePool> manager) noexcept {
        manager_ = std::move(manager);
    }

    [[nodiscard]] std::weak_ptr<TexturePool> manager() const noexcept {
        return manager_;
    }
private:
    uint32_t width_;
    uint32_t height_;
    TextureConfig config_;
    GLuint textureId_ = 0;
    std::weak_ptr<TexturePool> manager_;
};

using TexturePtr = std::unique_ptr<Texture>;
using TextureRefPtr = std::shared_ptr<Texture>;

} // namespace slark