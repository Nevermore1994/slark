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
#include "AudioInfo.h"
#include "BaseClass.hpp"


namespace slark {

enum class DemuxerType {
    Unknown = 0,
    FLV,
    MP4,
    HLS,
    DASH,
    WAV,
};

enum class DemuxerState {
    Unknown = -1,
    Success = 0,
    Failed = 1,
    NotSupport = 2,
    FileEnd = 3,
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

class IDemuxer : public slark::NonCopyable {

public:
    ~IDemuxer() override = default;

public:
    virtual std::tuple<bool, uint64_t> open(std::string_view probeData) = 0;

    virtual void close() = 0;

    virtual void reset() = 0;

    virtual std::tuple<DemuxerState, AVFrameList> parseData(std::unique_ptr<Data> data) = 0;

    inline bool isInited() const noexcept {
        return isInited_;
    }

    inline bool isCompleted() const noexcept {
        return state_ == DemuxerState::FileEnd;
    }

    inline bool hasVideo() const noexcept {
        return videoInfo_ != nullptr;
    }

    inline bool hasAudio() const noexcept {
        return audioInfo_ != nullptr;
    }

    inline const VideoInfo* videoInfo() const noexcept {
        return videoInfo_.get();
    }

    inline const AudioInfo* audioInfo() const noexcept {
        return audioInfo_.get();
    }

    inline const DemuxerHeaderInfo& headerInfo() const noexcept {
        return headerInfo_;
    }

protected:
    bool isInited_ = false;
    DemuxerState state_;
    uint32_t parseFrameCount_ = 0;
    uint64_t parseLength_ = 0;
    std::unique_ptr<Data> overflowData_;
    std::unique_ptr<VideoInfo> videoInfo_;
    std::unique_ptr<AudioInfo> audioInfo_;
    DemuxerHeaderInfo headerInfo_;
};

template<typename T>
std::unique_ptr<IDemuxer> createDemuxer(std::string name) {
    return std::unique_ptr<IDemuxer>(BaseClass<T>::create(name));
}

}//end namespace slark

/* IDemuxer_hpp */
