//
//  AVFrame.hpp
//  slark
//
//  Created by Nevermore on 2022/4/29.
//  Copyright (c) 2023 Nevermore All rights reserved.

#pragma once

#include <any>
#include <cstdint>
#include <exception>
#include <memory>
#include <expected>
#include "Assert.hpp"
#include "Data.hpp"
#include "Log.hpp"
namespace slark {

enum class AVFrameType {
    Unknown = 0,
    Audio,
    Video,
};

enum class FrameFormat {
    Unknown = 0,
    VideoToolBox = 1,
};

struct Statistics {
    uint64_t demuxStamp = 0;
    uint64_t pendingStamp = 0;
    uint64_t prepareDecodeStamp = 0;
    uint64_t decodedStamp = 0;
    uint64_t prepareRenderStamp = 0;
    uint64_t renderedStamp = 0;
};

struct AudioFrameInfo {
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint64_t sampleRate = 0;
    
    double duration(uint64_t size) const noexcept {
        return static_cast<double>(size) / static_cast<double>(bitsPerSample / 8 * channels * sampleRate);
    }
};

enum class VideoFrameType {
    Unknown,
    IFrame,
    PFrame,
    BFrame,
    SEI,
    SPS [[maybe_unused]],
    PPS [[maybe_unused]],
};

struct VideoFrameInfo {
    bool isKeyFrame = false;
    VideoFrameType frameType = VideoFrameType::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t offset = 0;
    uint64_t keyIndex = 0;
    FrameFormat format = FrameFormat::Unknown;
    
    bool isHasContent() const {
        static const std::vector<VideoFrameType> kHasContentFrames = { VideoFrameType::IFrame, VideoFrameType::PFrame, VideoFrameType::BFrame };
        return std::any_of(kHasContentFrames.begin(), kHasContentFrames.end(), [this](auto type){
            return type == frameType;
        });
    }
};

struct AVFrame {
    bool isDiscard = false;
    AVFrameType frameType = AVFrameType::Unknown;
    uint32_t duration = 0; //ms
    uint64_t timeScale = 0;
    uint64_t index = 0;
    uint64_t pts = 0;
    uint64_t dts = 0;
    uint64_t offset = 0;
    std::unique_ptr<Data> data; //Undecoded
    void* opaque = nullptr;
    Statistics stats;
    std::any info;

    inline void reset() noexcept {
        frameType = AVFrameType::Unknown;
        index = 0;
        info.reset();
        pts = 0;
        dts = 0;
        data->destroy();
    }

    inline std::unique_ptr<Data> detachData() noexcept {
        auto d = std::move(data);
        data.reset();
        return d;
    }
    
    long double dtsTime() const noexcept {
        return std::round(static_cast<long double>(dts) / static_cast<long double>(timeScale) * 1000) / 1000;
    }
    
    long double ptsTime() const noexcept {
        return std::round(static_cast<long double>(pts) / static_cast<long double>(timeScale) * 1000) / 1000;
    }

    [[nodiscard]] inline std::unique_ptr<AVFrame> copy() const noexcept {
        auto frame = std::make_unique<AVFrame>();
        frame->frameType = frameType;
        frame->index = index;
        frame->pts = pts;
        frame->dts = dts;
        frame->offset = offset;
        frame->timeScale = timeScale;
        frame->duration = duration;
        if (data) {
            frame->data = data->copy();
        }
        frame->info = info;
        return frame;
    }

    bool operator<(const AVFrame& rhs) const noexcept {
        return index < rhs.index;
    }

    bool operator>(const AVFrame& rhs) const noexcept {
        return index > rhs.index;
    }

    [[nodiscard]] inline bool isVideo() const noexcept {
        return frameType == AVFrameType::Video;
    }

    [[nodiscard]] inline bool isAudio() const noexcept {
        return frameType == AVFrameType::Audio;
    }

    [[nodiscard]] std::expected<AudioFrameInfo, bool> audioInfo() const noexcept {
        if (isVideo()) {
            std::unexpected(false);
        }
        return std::any_cast<AudioFrameInfo>(info);
    }

    [[nodiscard]] std::expected<VideoFrameInfo, bool> videoInfo() const noexcept {
        if (isAudio()) {
            std::unexpected(false);
        }
        return std::any_cast<VideoFrameInfo>(info);
    }
    
    std::string_view view() const noexcept {
        if (data) {
            return data->view();
        }
        return {};
    }
    
    
};

using AVFramePtr = std::unique_ptr<AVFrame>;
using AVFrameRefPtr = std::shared_ptr<AVFrame>;
using AVFrameRef = AVFrame&;
using AVFrameArray = std::vector<AVFramePtr>;
}
