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

struct DemuxerConfig {
    uint64_t fileSize = 0;
    std::string filePath;
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
    FileEnd,
    ParsedHeader,
    VideoInfoChange,
    ParsedFPS,
};

struct DemuxerResult {
    DemuxerResultCode resultCode = DemuxerResultCode::Normal;
    AVFramePtrArray audioFrames;
    AVFramePtrArray videoFrames;
};

class IDemuxer : public NonCopyable {

public:
    ~IDemuxer() override = default;

public:
    virtual bool open(std::unique_ptr<Buffer>&) = 0;

    virtual void close() = 0;

    virtual void reset() noexcept {
        isOpened_ = false;
        isCompleted_ = false;
        totalDuration_ = CTime(0);
        buffer_.reset();
        videoInfo_.reset();
        audioInfo_.reset();
    }
    
    virtual void seekPos(uint64_t pos) noexcept {
        isCompleted_ = false;
        if (buffer_) {
            buffer_->reset();
            buffer_->setOffset(pos);
        }
    }

    virtual DemuxerResult parseData(DataPacket& packet) noexcept = 0;
     
    [[nodiscard]] virtual uint64_t getSeekToPos(long double) noexcept = 0;

    [[nodiscard]] bool isOpened() const noexcept {
        return isOpened_;
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

    [[nodiscard]] const std::shared_ptr<AudioInfo>& audioInfo() const noexcept {
        return audioInfo_;
    }

    [[nodiscard]] const std::shared_ptr<DemuxerHeaderInfo>& headerInfo() const noexcept {
        return headerInfo_;
    }
    
    [[nodiscard]] CTime totalDuration() const noexcept {
        if (!isOpened_) {
            return CTime();
        }
        return totalDuration_;
    }
    
    [[nodiscard]] DemuxerType type() const noexcept {
        return type_;
    }
    
    virtual void init(DemuxerConfig config) noexcept{
        config_ = std::move(config);
    }
    
protected:
    bool isOpened_ = false;
    bool isCompleted_ = false;
    DemuxerConfig config_;
    DemuxerType type_ = DemuxerType::Unknown;
    CTime totalDuration_{0};
    std::unique_ptr<Buffer> buffer_;
    std::shared_ptr<VideoInfo> videoInfo_;
    std::shared_ptr<AudioInfo> audioInfo_;
    std::shared_ptr<DemuxerHeaderInfo> headerInfo_;
};

}//end namespace slark

/* IDemuxer_hpp */
