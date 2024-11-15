//
//  VideoInfo.h
//  slark
//
//  Created by Nevermore.
//

#pragma once

#include "Data.hpp"
#include "MediaDefs.h"

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
};

struct IVideoRender {
    virtual void start() noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual void notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept = 0;
    virtual void notifyRenderInfo() noexcept = 0;
    virtual ~IVideoRender() = default;
};

} //end namespace slark

