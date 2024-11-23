//
//  VideoInfo.h
//  slark
//
//  Created by Nevermore.
//

#pragma once

#include "Data.hpp"
#include "MediaDefs.h"
#include "Clock.h"

namespace slark {

struct VideoInfo {
    uint16_t fps{};
    uint16_t naluHeaderLength;
    uint32_t width{};
    uint32_t height{};
    uint32_t timeScale{};
    std::string_view mediaInfo;
    DataRefPtr sps;
    DataRefPtr pps;
    DataRefPtr vps;
    
    long double frameDuration() const noexcept {
        return 1.0 / static_cast<long double>(fps);
    }
};

struct IVideoRender {
    virtual void start() noexcept = 0;
    virtual void pause() noexcept = 0;
    virtual void notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept = 0;
    virtual void notifyRenderInfo() noexcept = 0;
    virtual void pushVideoFrameRender(void* frame) noexcept = 0;
    virtual ~IVideoRender() = default;
    Clock& clock() noexcept {
        return videoClock_;
    }
protected:
    Clock videoClock_;
};

} //end namespace slark

