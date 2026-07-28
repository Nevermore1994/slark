//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <memory>
#include <optional>
#include <tuple>

#include "AVFrame.hpp"
#include "AudioInfo.h"
#include "Buffer.hpp"
#include "NonCopyable.h"
#include "Reflection.hpp"
#include "VideoInfo.h"

namespace slark {

enum class DemuxerType {
    Unknown = 0,
    FLV,
    MP4,
    HLS,
    WAV,
};

struct DemuxerConfig {
    std::string filePath;
};

struct DemuxerInfo {
    DemuxerType type = DemuxerType::Unknown;
    std::string symbol;
    std::string demuxerName;
    uint64_t symbolOffset = 0;
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
    InvalidData,
};

struct DemuxerResult {
    DemuxerResultCode resultCode = DemuxerResultCode::Normal;
    AVFramePtrArray audioFrames;
    AVFramePtrArray videoFrames;
};

class IDemuxer : public NonCopyMovable {
 public:
    virtual ~IDemuxer() = default;

 public:
    virtual bool open(std::unique_ptr<Buffer>&) noexcept = 0;

    virtual void close() noexcept = 0;

    virtual void reset() noexcept {
        isOpened_ = false;
        isCompleted_ = false;
        totalDuration_ = CTime(0);
        buffer_.reset();
        videoInfo_.reset();
        audioInfo_.reset();
    }

    virtual void flush() noexcept {
        isCompleted_ = false;
        if (buffer_) {
            buffer_->reset();
        }
    }

    virtual void seekPos(uint64_t pos) noexcept {
        isCompleted_ = false;
        if (buffer_) {
            buffer_->reset();
            buffer_->setOffset(pos);
        }
    }

    virtual DemuxerResult parseData(DataPacket& packet) noexcept = 0;

    [[nodiscard]] virtual uint64_t getSeekToPos(double) noexcept = 0;

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

    [[nodiscard]] std::shared_ptr<VideoInfo> videoInfo() noexcept {
        return videoInfo_;
    }

    [[nodiscard]] std::shared_ptr<AudioInfo> audioInfo() noexcept {
        return audioInfo_;
    }

    [[nodiscard]] std::shared_ptr<DemuxerHeaderInfo> headerInfo() noexcept {
        return headerInfo_;
    }

    [[nodiscard]] CTime totalDuration() const noexcept {
        if (!isOpened_) {
            return {};
        }
        return totalDuration_;
    }

    virtual CTime computeTotalDuration() const noexcept {
        return totalDuration_;
    }

    [[nodiscard]] DemuxerType type() const noexcept {
        return type_;
    }

    [[nodiscard]] uint64_t offset() const noexcept {
        if (buffer_) {
            return buffer_->offset();
        }
        return 0;
    }

    virtual void init(DemuxerConfig config) noexcept {
        config_ = std::move(config);
    }

    [[nodiscard]] std::optional<uint64_t> pendingSeekPos() const noexcept {
        return pendingSeekPos_;
    }

    void resetPendingSeek() noexcept {
        pendingSeekPos_.reset();
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
    std::optional<uint64_t> pendingSeekPos_;
};

}  // end namespace slark
