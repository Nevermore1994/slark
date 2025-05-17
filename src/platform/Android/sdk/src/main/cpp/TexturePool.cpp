//
// Created by Nevermore- on 2025/5/15.
//

#include "TexturePool.h"

namespace slark {

TexturePtr TexturePool::acquire(uint32_t width, uint32_t height, TextureConfig config) {
    for (auto it = pool_.begin(); it != pool_.end(); ++it) {
        if ((*it)->width() == width && (*it)->height() == height &&
            (*it)->config().internalFormat == config.internalFormat) {
            TexturePtr tex = std::move(*it);
            pool_.erase(it);
            return tex;
        }
    }
    auto ptr = std::make_unique<Texture>(width, height, config);
    if (!ptr->isValid()) {
        return nullptr;
    }
    ptr->setManager(weak_from_this());
    return ptr;
}

void TexturePool::release(TexturePtr texture) {
    if (!texture || !texture->isValid()) {
        return;
    }
    if (pool_.size() < maxSize_) {
        pool_.emplace_back(std::move(texture));
    }
    //discard
}

} // slark