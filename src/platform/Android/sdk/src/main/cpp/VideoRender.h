//
// Created by Nevermore- on 2025/5/13.
//

#pragma once

#include "Clock.h"
#include "Manager.hpp"
#include "Texture.h"
#include "VideoInfo.h"

namespace slark {

struct RenderInfo {
    double ptsTime = 0;
    TexturePtr texture = nullptr;

    RenderInfo() = default;

    RenderInfo(double ptsTime, TexturePtr texture)
        : ptsTime(ptsTime), texture(std::move(texture)) {}

    [[nodiscard]] bool isValid() const noexcept {
        return texture && texture->isValid();
    }
};

class VideoRender : public IVideoRender, public std::enable_shared_from_this<VideoRender> {
public:
    VideoRender();

    ~VideoRender() noexcept override;

    void start() noexcept override;

    void pause() noexcept override;

    void renderEnd() noexcept override;

    void notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept override;

    void notifyRenderInfo() noexcept override;

    void pushVideoFrameRender(AVFrameRefPtr frame) noexcept override;

    void renderFrameComplete(int32_t id) noexcept;

    /// Called from Kotlin Choreographer.doFrame() via JNI on each VSync beat.
    void onVSync() noexcept;

    TexturePtr& getBackupTexture() noexcept {
        std::lock_guard lock(mutex_);
        return backupRenderInfo_.texture;
    }

public:
    std::mutex mutex_;
    std::unordered_map<uint32_t, RenderInfo> renderInfos_;
    RenderInfo backupRenderInfo_;

private:
    void requestRenderFrame() noexcept;

    void renderFrame(const AVFrameRefPtr& frame) noexcept;

    Clock videoClock_;
};

using VideoRenderManager = Manager<VideoRender>;

} // namespace slark
