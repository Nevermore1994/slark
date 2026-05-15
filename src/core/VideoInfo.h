//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AVFrame.hpp"
#include "IPipelineComponent.h"
#include "Synchronized.hpp"
#include "Time.hpp"

namespace slark {

struct VideoInfo {
    uint16_t fps{};
    uint16_t naluHeaderLength;
    uint32_t width{};
    uint32_t height{};
    uint32_t timeScale{};
    uint8_t profile{};
    uint8_t level{};
    uint8_t numRefFrames{}; // H.264 SPS num_ref_frames / H.265 sps_max_dec_pic_buffering
    std::string mediaInfo;
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

struct IVideoRender : public IPipelineComponent {
    void start() noexcept override = 0;
    void pause() noexcept override = 0;

    // VideoRender 无需 close / flush 资源释放，提供默认空实现
    void close() noexcept override {}

    void flush() noexcept override {}

    [[nodiscard]] bool isRunning() const noexcept override {
        return false;
    }

    virtual void notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept = 0;
    virtual void notifyRenderInfo() noexcept = 0;
    virtual void renderEnd() noexcept = 0;

    /// Push 模式：仅用于起播首帧和 seek 首帧的快速上屏，其余帧由 VSync 驱动拉取
    virtual void pushFrame(AVFrameRefPtr frame) noexcept = 0;

    virtual ~IVideoRender() = default;

    void setRequestRenderFunc(RequestRenderFunc func) noexcept {
        auto ptr = std::make_shared<RequestRenderFunc>(std::move(func));
        requestRenderFunc_.reset(ptr);
    }

    void setPlayerId(std::string_view playerId) noexcept {
        playerId_ = playerId;
    }

protected:
    AtomicSharedPtr<RequestRenderFunc> requestRenderFunc_;
    std::string playerId_;
};

} // namespace slark
