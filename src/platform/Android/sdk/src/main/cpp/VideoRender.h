//
// Created by Nevermore- on 2025/5/13.
//

#pragma once

#include "VideoInfo.h"
#include "Thread.h"
#include "Manager.hpp"
#include "Texture.h"

namespace slark {

struct RenderInfo {
    double ptsTime = 0;
    TexturePtr texture;

    RenderInfo() = default;
    RenderInfo(double ptsTime, TexturePtr texture)
        : ptsTime(ptsTime), texture(std::move(texture)) {

    }
};

class VideoRender
    : public IVideoRender,
      public std::enable_shared_from_this<VideoRender> {
public:
    VideoRender();

    ~VideoRender() noexcept override;

    void start() noexcept override;

    void pause() noexcept override;

    void notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept override;

    void notifyRenderInfo() noexcept override;

    void pushVideoFrameRender(AVFrameRefPtr frame) noexcept override;

    void renderComplete(int32_t textureId) noexcept;

private:
    void requestRender() noexcept;

    void renderFrame(const AVFrameRefPtr &frame) noexcept;

public:
    std::unique_ptr<Thread> renderThread_;
    std::mutex mutex_;
    std::unordered_map<uint32_t, RenderInfo> renderInfos_;
};

using VideoRenderManager = Manager<VideoRender>;

} // slark

