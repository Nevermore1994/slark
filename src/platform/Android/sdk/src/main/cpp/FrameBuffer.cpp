//
// Created by Nevermore- on 2025/5/7.
//

#include "FrameBuffer.h"
#include "Log.hpp"
#include "TexturePool.h"
#include <utility>

namespace slark {

FrameBuffer::FrameBuffer(TexturePtr texture)
    : texture_(std::move(texture)) {
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
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture_->textureId(), 0);
    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LogE("Framebuffer is not complete: {}", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        release();
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    activeThreads_.insert(std::this_thread::get_id());
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

void FrameBuffer::bind(bool isRebinding) noexcept {
    if (isRebinding) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFrameBufferId_);
    }
    if (frameBufferId_ && texture_) {
        if (activeThreads_.contains(std::this_thread::get_id())) {
            return;
        }
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texture_->textureId(), 0);
        glBindFramebuffer(GL_FRAMEBUFFER, frameBufferId_);
        activeThreads_.insert(std::this_thread::get_id());
    } else {
        LogE("Framebuffer is not valid");
    }
}

void FrameBuffer::unbind(bool isRollback) noexcept {
    if (isRollback) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFrameBufferId_));
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

} // slark