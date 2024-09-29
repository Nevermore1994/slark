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
#include "Buffer.hpp"

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

    virtual void reset() noexcept {
        isInited_ = false;
        isCompleted_ = false;
        receivedLength_ = 0;
        prasedLength_ = 0;
        parsedFrameCount_ = 0;
        totalDuration_ = CTime(0);
        seekPos_.reset();
        buffer_.reset();
        videoInfo_.reset();
        audioInfo_.reset();
    }
    
    virtual void seekPos(uint64_t pos) noexcept {
        buffer_.reset();
        receivedLength_ = pos;
        prasedLength_ = pos;
        seekPos_ = pos;
    }

    virtual DemuxerResult parseData(std::unique_ptr<Data> data, int64_t offset) noexcept = 0;
     
    [[nodiscard]] virtual uint64_t getSeekToPos(Time::TimePoint) noexcept = 0;

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

    [[nodiscard]] const std::shared_ptr<DemuxerHeaderInfo>& headerInfo() const noexcept {
        return headerInfo_;
    }
    
    [[nodiscard]] CTime startTime() const noexcept {
        if (!isInited_) {
            return CTime();
        }
        if (videoInfo_ && audioInfo_) {
            return std::min(videoInfo_->startTime, audioInfo_->startTime);
        } else if (videoInfo_) {
            return videoInfo_->startTime;
        } else if (audioInfo_) {
            return audioInfo_->startTime;
        }
        return CTime();
    }
    
    [[nodiscard]] CTime totalDuration() const noexcept {
        if (!isInited_) {
            return CTime();
        }
        return totalDuration_;
    }
    
protected:
    bool isInited_ = false;
    bool isCompleted_ = false;
    uint32_t parsedFrameCount_ = 0;
    uint64_t receivedLength_ = 0;
    uint64_t prasedLength_ = 0;
    std::optional<int64_t> seekPos_;
    CTime totalDuration_{0};
    Buffer buffer_;
    std::shared_ptr<VideoInfo> videoInfo_;
    std::shared_ptr<Audio::AudioInfo> audioInfo_;
    std::shared_ptr<DemuxerHeaderInfo> headerInfo_;
};

}//end namespace slark

/* IDemuxer_hpp */
