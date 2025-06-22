//
// Created by Nevermore- on 2025/6/22.
//

#pragma once

#include "AndroidEGLContext.h"
#include "FrameBufferPool.h"
#include "Manager.hpp"

namespace slark {

class VideoDecodeResource {
public:
    explicit VideoDecodeResource(std::string playerId);

    ~VideoDecodeResource() noexcept;

    void detachContext() noexcept;

    EGLSurface createOffscreenSurface(uint32_t width, uint32_t height) noexcept;

    void releaseSurface(EGLSurface surface) noexcept;

    void attachContext(EGLSurface surface) noexcept;

    FrameBufferRefPtr acquire(
        uint32_t width,
        uint32_t height
    ) noexcept;

    [[nodiscard]] bool isValid() const noexcept {
        return context_ != nullptr && frameBufferPool_ != nullptr;
    }

    void init() noexcept;
private:
    std::string playerId_;
    std::shared_ptr<AndroidEGLContext> context_;
    std::unique_ptr<FrameBufferPool> frameBufferPool_;
};

using VideoDecodeResourceManager = Manager<VideoDecodeResource>;

} // slark


