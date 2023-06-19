//
//  AVFrame.hpp
//  Slark
//
//  Created by Nevermore on 2022/4/29.
//

#pragma once

#include <cstdint>
#include <memory>
#include <algorithm>
#include <list>
#include "Data.hpp"

namespace Slark {

enum class AVFrameType {
    Unknown = 0,
    Audio,
    Video,
};

struct AVFrame {
    bool isKeyFrame = false;
    AVFrameType frameType = AVFrameType::Unknown;
    uint32_t duration = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t offset = 0;
    uint64_t index = 0;
    uint64_t decodedFrameIndex = 0;
    uint64_t refIndex = 0;
    uint64_t assembleFrameStamp = 0;
    uint64_t pendingStamp = 0;
    uint64_t prepareDecodeStamp = 0;
    uint64_t encodedStamp = 0;
    uint64_t decodedStamp = 0;
    uint64_t prepareRenderStamp = 0;
    uint64_t pushRenderQueueStamp = 0;
    uint64_t popRenderQueueStamp = 0;
    uint64_t renderedStamp = 0;
    uint64_t pts = 0;
    uint64_t dts = 0;
    uint64_t keyIndex = 0;
    Data data;

    inline void reset() noexcept {
        isKeyFrame = false;
        frameType = AVFrameType::Unknown;
        duration = 0;
        width = 0;
        height = 0;
        offset = 0;
        index = 0;
        decodedFrameIndex = 0;
        refIndex = 0;
        assembleFrameStamp = 0;
        pendingStamp = 0;
        prepareDecodeStamp = 0;
        encodedStamp = 0;
        decodedStamp = 0;
        prepareRenderStamp = 0;
        pushRenderQueueStamp = 0;
        popRenderQueueStamp = 0;
        renderedStamp = 0;
        pts = 0;
        dts = 0;
        data.release();
    }

    inline void resetData() const noexcept {
        data.reset();
    }

    inline Data detachData() noexcept {
        auto d = data;
        data = Data();
        return d;
    }

    inline std::unique_ptr<AVFrame> copy() const noexcept {
        auto frame = std::make_unique<AVFrame>();
        frame->isKeyFrame = this->isKeyFrame;
        frame->frameType = this->frameType;
        frame->duration = this->duration;
        frame->width = this->width;
        frame->height = this->height;
        frame->offset = this->offset;
        frame->index = this->index;
        frame->decodedFrameIndex = this->decodedFrameIndex;
        frame->refIndex = this->refIndex;
        frame->assembleFrameStamp = this->assembleFrameStamp;
        frame->pendingStamp = this->pendingStamp;
        frame->prepareDecodeStamp = this->prepareDecodeStamp;
        frame->encodedStamp = this->encodedStamp;
        frame->decodedStamp = this->decodedStamp;
        frame->prepareRenderStamp = this->prepareRenderStamp;
        frame->pushRenderQueueStamp = this->pushRenderQueueStamp;
        frame->popRenderQueueStamp = this->popRenderQueueStamp;
        frame->renderedStamp = this->renderedStamp;
        frame->pts = this->pts;
        frame->dts = this->dts;
        frame->data = data.copy();
        return frame;
    }

    constexpr bool operator<(const AVFrame& rhs) const noexcept {
        return index < rhs.index;
    }

    constexpr bool operator>(const AVFrame& rhs) const noexcept {
        return index > rhs.index;
    }
};

using AVFramePtr = std::unique_ptr<AVFrame>;
using AVFrameRef = AVFrame*;
using AVFrameList = std::list<AVFramePtr>;
}
