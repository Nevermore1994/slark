//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "FrameBuffer.h"

#include "Log.hpp"
#include "TexturePool.h"

#include <utility>

namespace slark {

FrameBuffer::FrameBuffer(TexturePtr texture) : texture_(std::move(texture)) {
    init();
}

FrameBuffer::~FrameBuffer() {
    release();
}

bool FrameBuffer::init() noexcept {
    if (!texture_) {
        return false;
    }
    glGenFramebuffers(1, &frameBufferId_);
    if (frameBufferId_ == 0) {
        LogE("Failed to create framebuffer");
        release();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, frameBufferId_);
    glBindTexture(GL_TEXTURE_2D, texture_->textureId());
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           texture_->textureId(),
                           0);
    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LogE("Framebuffer is not complete: {}", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        release();
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void FrameBuffer::update(TexturePtr texture) noexcept {
    release();
    texture_ = std::move(texture);
    if (texture_ && !init()) {
        LogE("Failed to update framebuffer");
    }
}

void FrameBuffer::release() noexcept {
    if (frameBufferId_) {
        glDeleteFramebuffers(1, &frameBufferId_);
        frameBufferId_ = 0;
    }
    texture_.reset();
}

bool FrameBuffer::bind(bool isRebinding) noexcept {
    if (!frameBufferId_ || !texture_) {
        LogE("Framebuffer is not valid");
        return false;
    }
    if (isRebinding) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFrameBufferId_);
    }
    // 必须先 bind FBO，再 attach texture，再 check status
    glBindFramebuffer(GL_FRAMEBUFFER, frameBufferId_);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           texture_->textureId(),
                           0);
    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LogE("Framebuffer is not complete: {}", status);
        unbind(isRebinding);
        return false;
    }
    return true;
}

void FrameBuffer::unbind(bool isRollback) noexcept {
    if (isRollback) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFrameBufferId_));
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}
} // namespace slark