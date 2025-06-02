//
//  VideoInfo.h
//  slark
//
//  Created by Nevermore.
//

#pragma once

#include "Clock.h"
#include "AVFrame.hpp"
#include "Synchronized.hpp"

namespace slark {

struct VideoInfo {
    uint16_t fps{};
    uint16_t naluHeaderLength;
    uint32_t width{};
    uint32_t height{};
    uint32_t timeScale{};
    uint8_t profile{};
    uint8_t level{};
    std::string_view mediaInfo;
    DataRefPtr sps;
    DataRefPtr pps;
    DataRefPtr vps;
    
    double frameDuration() const noexcept {
        if (fps == 0) {
            return 33.0 / 1000.0;
        }
        return 1.0 / static_cast<double>(fps);
    }

    std::chrono::milliseconds frameDurationMs() const noexcept {
        return std::chrono::milliseconds(static_cast<uint32_t>(frameDuration() * 1000));
    }
};

using RequestRenderFunc = std::function<AVFrameRefPtr()>;
struct IVideoRender {
    virtual void start() noexcept = 0;

    virtual void pause() noexcept = 0;

    virtual void notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept = 0;

    virtual void notifyRenderInfo() noexcept = 0;

    virtual void pushVideoFrameRender(AVFrameRefPtr frame) noexcept = 0;

    virtual ~IVideoRender() = default;

    Clock& clock() noexcept {
        return videoClock_;
    }

    void setRequestRenderFunc(RequestRenderFunc func) noexcept {
        auto ptr = std::make_shared<RequestRenderFunc>(std::move(func));
        requestRenderFunc_.reset(ptr);
    }

    void setPlayerId(std::string_view playerId) noexcept {
        playerId_ = playerId;
    }

protected:
    Clock videoClock_;
    AtomicSharedPtr<RequestRenderFunc> requestRenderFunc_;
    std::string playerId_;
};

} //end namespace slark

