//
// Created by Nevermore- on 2025/5/6.
//

#include "Texture.h"
#include "Log.hpp"
#include "TexturePool.h"

namespace slark {

Texture::Texture(uint32_t width,
                 uint32_t height,
                 TextureConfig config,
                 DataPtr data
 ): width_(width)
 , height_(height)
 , config_(config) {
    glGenTextures(
        1,
        &textureId_
    );
    bind();
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        config_.warp
            .s
    );
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        config_.warp
            .t
    );
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        config_.filter
            .min
    );
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        config_.filter
            .mag
    );
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        config_.internalFormat,
        static_cast<int>(width_),
        static_cast<int>(height_),
        0,
        config_.format,
        config_.type,
        data ? data->rawData : nullptr
    );
    glBindTexture(
        GL_TEXTURE_2D,
        0
    );
}

Texture::~Texture() {
    if (textureId_ != 0) {
        glDeleteTextures(1,&textureId_);
        textureId_ = 0;
    }
}

void Texture::updateData(DataPtr data) const noexcept {
    if (!data) {
        return;
    }
    glBindTexture(
        GL_TEXTURE_2D,
        textureId_
    );
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        config_.internalFormat,
        static_cast<int>(width_),
        static_cast<int>(height_),
        0,
        config_.format,
        config_.type,
        data->rawData
    );
    glBindTexture(
        GL_TEXTURE_2D,
        0
    );
}

void Texture::releaseResource(std::unique_ptr<Texture> texture) noexcept {
    auto manager = texture->manager().lock();
    if (manager) {
        manager->restore(std::move(texture));
    }
}

}
