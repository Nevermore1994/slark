//
//  demuxer.hpp
//  Slark
//
//  Created by Nevermore on 2022/4/26.
//

#pragma once

#include <tuple>
#include <memory>
#include "NonCopyable.hpp"
#include "AVFrame.hpp"
#include "VideoInfo.hpp"
#include "AudioInfo.hpp"
#include "BaseClass.hpp"


namespace Slark {

enum class DemuxerType {
    Unknown = 0,
    FLV,
    MP4,
    HLS,
    DASH,
    WAV,
};

enum class DemuxerState {
    Failed = 0,
    Success = 1,
    FileEnd = 2,
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

class IDemuxer : public Slark::NonCopyable {

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
        return isCompleted_;
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
    bool isCompleted_ = false;
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

}//end namespace Slark

/* IDemuxer_hpp */
