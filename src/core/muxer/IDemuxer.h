//
//  demuxer.hpp
//  slark
//
//  Created by Nevermore on 2022/4/26.
//

#pragma once

#include <tuple>
#include <memory>
#include "NonCopyable.h"
#include "AVFrame.hpp"
#include "VideoInfo.h"
#include "AudioDefine.h"
#include "Reflection.hpp"

namespace slark {

enum class DemuxerType {
    Unknown = 0,
    FLV,
    MP4,
    HLS,
    WAV,
};

struct DemuxerInfo {
    DemuxerType type = DemuxerType::Unknown;
    std::string symbol;
    std::string demuxerName;
};

struct DemuxerHeaderInfo {
    uint64_t headerLength = 0;
    uint64_t dataSize = 0;
};

enum class DemuxerResultCode {
    Normal = 0,
    Failed,
    NotSupport,
    FileEnd,
};

struct DemuxerResult {
    DemuxerResultCode resultCode = DemuxerResultCode::Normal;
    AVFrameArray audioFrames;
    AVFrameArray videoFrames;
};

class IDemuxer : public NonCopyable {

public:
    ~IDemuxer() override = default;

public:
    virtual std::tuple<bool, uint64_t> open(std::string_view probeData) = 0;

    virtual void close() = 0;

    virtual void reset() = 0;

    virtual DemuxerResult parseData(std::unique_ptr<Data> data) = 0;

    [[nodiscard]] bool isInited() const noexcept {
        return isInited_;
    }

    [[nodiscard]] bool isCompleted() const noexcept {
        return isCompleted_;
    }

    [[nodiscard]] bool hasVideo() const noexcept {
        return videoInfo_ != nullptr;
    }

    [[nodiscard]] bool hasAudio() const noexcept {
        return audioInfo_ != nullptr;
    }

    [[nodiscard]] const std::shared_ptr<VideoInfo>& videoInfo() const noexcept {
        return videoInfo_;
    }

    [[nodiscard]] const std::shared_ptr<Audio::AudioInfo>& audioInfo() const noexcept {
        return audioInfo_;
    }

    [[nodiscard]] const DemuxerHeaderInfo& headerInfo() const noexcept {
        return headerInfo_;
    }

protected:
    bool isInited_ = false;
    bool isCompleted_ = false;
    uint32_t parseFrameCount_ = 0;
    uint64_t parseLength_ = 0;
    std::unique_ptr<Data> overflowData_;
    std::shared_ptr<VideoInfo> videoInfo_;
    std::shared_ptr<Audio::AudioInfo> audioInfo_;
    DemuxerHeaderInfo headerInfo_;
};

}//end namespace slark

/* IDemuxer_hpp */
