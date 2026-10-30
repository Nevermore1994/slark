//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <memory>
#include <utility>

#include "PlayerTypes.h"
#include "VideoSink.h"

namespace slark::engine {

class VideoRenderSink final : public VideoSink {
 public:
    explicit VideoRenderSink(std::weak_ptr<IVideoRender> render) noexcept
        : render_(std::move(render)) {}

    void present(AVFrameRefPtr frame) override;
    void notifyVideoInfo(const std::shared_ptr<VideoInfo>& info) override;
    void dropFrames(int32_t count) override;
    [[nodiscard]] int32_t heldCount() const override;

 private:
    std::weak_ptr<IVideoRender> render_;
};

inline void VideoRenderSink::present(AVFrameRefPtr frame) {
    if (auto render = render_.lock()) {
        render->pushFrame(std::move(frame));
    }
}

inline void VideoRenderSink::notifyVideoInfo(const std::shared_ptr<VideoInfo>& info) {
    if (auto render = render_.lock()) {
        render->notifyVideoInfo(info);
    }
}

inline void VideoRenderSink::dropFrames(int32_t count) {
    (void)count;
}

inline int32_t VideoRenderSink::heldCount() const {
    // Always 0: a cumulative presented count would make the queue limit grow with playback time.
    return 0;
}

}  // namespace slark::engine
