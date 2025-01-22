//
//  HLSDemuxer.h
//  slark
//
//  Created by Nevermore on 2024/12/9.
//

#pragma once

#include "Buffer.hpp"
#include "IDemuxer.h"
#include "IOBase.h"
#include "Base.h"

namespace slark {

struct TSInfo {
    bool isDiscontinuty = false;
    bool isSupportByteRange = false;
    uint32_t sequence = 0;
    double duration = 0.0;
    double startTime = 0.0;
    std::string url;
    Range range;
    
    double endTime() const noexcept {
        return startTime + duration;
    }
};

struct PlayListInfo {
    uint32_t codeRate;
    std::string m3u8Url;
    std::string name;
};

struct M3U8Parser : public NonCopyable {
    M3U8Parser(const std::string& baseUrl);
    ~M3U8Parser() = default;
    void reset() noexcept;
    bool parse(Buffer& buffer) noexcept;
    
    const std::vector<PlayListInfo>& playListInfos() const noexcept {
        return playListInfos_;
    }
    
    const std::vector<TSInfo>& TSInfos() const noexcept {
        return infos_;
    }
    
    bool isPlayList() const noexcept {
        return isPlayList_;
    }
    
    bool isCompleted() const noexcept {
        return isCompleted_;
    }
    
    double totalDuration() const noexcept {
        return totalDuration_;
    }
private:
    bool isPlayList_ = false;
    bool isCompleted_ = false;
    uint32_t index = 0;
    double totalDuration_ = 0.0;
    std::string baseUrl_;
    std::vector<TSInfo> infos_;
    std::vector<PlayListInfo> playListInfos_;
};

struct TSHeader {
    uint8_t syncbyte = 0; //8 bit
    uint8_t errorIndicator = 0;//1 bit
    uint8_t payloadUinitIndicator = 0;//1 bit
    uint8_t priority = 0;//1 bit
    uint8_t scarmblingControl = 0;//2 bit
    uint8_t adaptationFieldControl = 0;//2 bit
    uint8_t continuityCounter = 0;//4 bit
    uint16_t pid = 0;//13 bit
};

struct TSPAT {
    uint8_t currentNextIndicator = 0; //1 bit
    uint8_t section = 0; //8 bit
    uint16_t sectionLength = 0; //12 bit
    uint16_t pmtPid = 0;
};

struct TSPMT {
    uint16_t sectionLength = 0;
    uint16_t progamInfoLength = 0;
    uint16_t audioPid = 0xff;
    uint16_t videoPid = 0xff;
    
    void reset() noexcept {
        sectionLength = 0;
        progamInfoLength = 0;
        audioPid = 0xff;
        videoPid = 0xff;
    }
};

struct TSPESFrame {
    uint64_t pts = 0;
    uint64_t dts = 0;
    Data mediaData;
    
    void reset() noexcept {
        pts = 0;
        dts = 0;
        mediaData.reset();
    }
};

struct TSParseInfo {
    struct Info {
        bool isValid = false;
        uint64_t firstDts = 0;
        uint64_t firstPts = 0;
        uint64_t frameIndex = 0;
        
        void reset() noexcept {
            isValid = false;
            firstDts = 0;
            firstPts = 0;
            frameIndex = 0;
        }
    };
    bool isNotifiedHeader = false;
    Info audioInfo;
    Info videoInfo;
    uint32_t calculatedFps = 0;
    
    bool isReady() const noexcept {
        return audioInfo.isValid && videoInfo.isValid;
    }
    
    void reset() noexcept {
        audioInfo.reset();
        videoInfo.reset();
    }
};

struct TSPtsFixInfo {
    static constexpr uint64_t kMaxPTS = (1ULL << 33);
    uint64_t preDtsTime = 0;
    uint64_t offset = 0;
    
    void reset() noexcept {
        preDtsTime = 0;
        offset = 0;
    }
};

struct AACAdtsHeader {
    uint8_t id = 0;
    uint8_t layer = 0;
    uint8_t hasCRC = 0;
    uint8_t profile = 0;
    uint8_t samplingIndex = 0;
    uint8_t channel = 0;
    uint16_t frameLength = 0;
    uint16_t bufferFullness = 0;
    uint8_t blockCount = 0;
};

class TSDemuxer {
    
public:
    TSDemuxer(std::shared_ptr<AudioInfo>& audioInfo,
              std::shared_ptr<VideoInfo>& videoInfo)
        : audioInfo_(audioInfo)
        , videoInfo_(videoInfo) {
        
    }
    
    void resetData() noexcept;
    
    bool parseData(Buffer& buffer, uint32_t tsIndex, DemuxerResult& result) noexcept;
private:
    bool checkPacket(Buffer& buffer, uint32_t& pos) noexcept;
    
    bool parsePacket(DataView data, uint32_t tsIndex, DemuxerResult& result) noexcept;
    
    bool parseTSPAT(DataView data) noexcept;
    
    bool parseTSPMT(DataView data) noexcept;
    
    bool parseTSPes(DataView data, uint8_t payloadIndicator, TSPESFrame& pes) noexcept;
    
    bool packH264VideoPacket(uint32_t tsIndex, AVFramePtrArray& frames) noexcept;
    
    void writeVideoData(AVFramePtr& frame, DataView view) noexcept;
    
    bool packAudioPacket(uint32_t tsIndex, AVFramePtrArray& frames) noexcept;
    
    void recalculatePtsDts(uint64_t& pts, uint64_t& dts, bool isAudio) noexcept;
private:
    static constexpr uint32_t kPacketSize = 188;
    TSPAT pat_;
    TSPMT pmt_;
    TSPESFrame audioESFrame_;
    TSPESFrame videoESFrame_;
    TSParseInfo parseInfo_;
    TSPtsFixInfo audioFixInfo_;
    TSPtsFixInfo videoFixInfo_;
    std::shared_ptr<AudioInfo>& audioInfo_;
    std::shared_ptr<VideoInfo>& videoInfo_;
};

class HLSDemuxer : public IDemuxer {
public:
    HLSDemuxer() = default;
    
    ~HLSDemuxer() override = default;
    
    void init(DemuxerConfig config) noexcept override;
    
    bool open(std::unique_ptr<Buffer>& buffer) noexcept override;
    
    void close() override;
    
    void seekPos(uint64_t pos) noexcept override;
    
    DemuxerResult parseData(DataPacket& packet) noexcept override;
    
    ///In HLS, this function obtains the ts index, not the offset of the file
    uint64_t getSeekToPos(long double) noexcept override;
    
    const std::vector<TSInfo>& getTSInfos() const noexcept;
    
    inline static const DemuxerInfo& info() noexcept {
        static DemuxerInfo info = {
            DemuxerType::HLS,
            "#EXTM3U",
            BaseClass<HLSDemuxer>::registerClass(GetClassName(HLSDemuxer))
        };
        return info;
    }
    
    bool isPlayList() const noexcept {
        if (mainParser_) {
            mainParser_->isPlayList();
        }
        return false;
    }
    
    const std::vector<PlayListInfo>& playListInfos() const noexcept {
        return mainParser_->playListInfos();
    }
    
private:
    static constexpr uint64_t kInvalidTSIndex = UINT64_MAX;
    uint64_t seekTsIndex_ = kInvalidTSIndex;
    std::unique_ptr<M3U8Parser> mainParser_;
    std::unique_ptr<M3U8Parser> slinkParser_;
    std::unique_ptr<TSDemuxer> tsDemuxer_;
    std::string baseUrl_;
};



}
