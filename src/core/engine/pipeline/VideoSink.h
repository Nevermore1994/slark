//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <memory>

#include "AVFrame.hpp"
#include "PlayerTypes.h"
#include "VideoInfo.h"

namespace slark::engine {

class VideoSink {
 public:
    virtual ~VideoSink() = default;

    virtual void present(AVFrameRefPtr frame) = 0;

    // Called once per session, on the session thread, before frames arrive (rotation, fps).
    virtual void notifyVideoInfo(const std::shared_ptr<VideoInfo>& info) {
        (void)info;
    }

    virtual void dropFrames(int32_t count) = 0;

    [[nodiscard]] virtual int32_t heldCount() const = 0;
};

}  // namespace slark::engine
