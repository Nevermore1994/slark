//
// Created by Nevermore- on 2025/5/7.
//

#pragma once
#include "Texture.h"
#include <unordered_set>
#include <thread>

namespace slark {

class FrameBuffer {
public:
    explicit FrameBuffer(TexturePtr texture = nullptr);

    ~FrameBuffer();

    void bind(bool isRebinding = false) noexcept;

    void unbind(bool isRollback = false) noexcept;

    bool init(uint32_t width, uint32_t height) noexcept;

    void update(TexturePtr texture) noexcept;

    [[nodiscard]] bool isValid() const noexcept {
        return frameBufferId_ != 0;
    }

    [[nodiscard]] uint32_t width() const noexcept {
        return texture_ ? texture_->width() : 0;
    }

    [[nodiscard]] uint32_t height() const noexcept {
        return texture_ ? texture_->height() : 0;
    }

    [[nodiscard]] GLuint frameBufferId() const noexcept {
        return frameBufferId_;
    }

    [[nodiscard]] const TexturePtr& texture() const noexcept {
        return texture_;
    }

    TexturePtr detachTexture() noexcept {
        auto p = std::move(texture_);
        texture_.reset();
        return p;
    }

private:
    bool init() noexcept;

    void release() noexcept;
private:
    GLint prevFrameBufferId_ = 0;
    GLuint frameBufferId_ = 0;
    TexturePtr texture_;
    std::unordered_set<std::thread::id> activeThreads_;
};

using FrameBufferRefPtr = std::shared_ptr<FrameBuffer>;

} // slark


