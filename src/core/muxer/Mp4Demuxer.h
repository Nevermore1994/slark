//
//  Mp4Demuxer.h
//  slark
//
//  Created by Nevermore
//

#pragma once

#include "IDemuxer.h"
#include "Mp4Box.hpp"

namespace slark {

enum class TrackType {
    Unknown = 0,
    Audio = 1,
    Video = 2,
};
class TrackContext {
public:
    bool isCompleted = false;
    TrackType type = TrackType::Unknown;
    CodecId codecId = CodecId::Unknown;
    std::shared_ptr<BoxMdhd> mdhd;
    std::shared_ptr<Box> stbl;
    std::shared_ptr<BoxStco> stco;
    std::shared_ptr<BoxStsz> stsz;
    std::shared_ptr<BoxStsc> stsc;
    std::shared_ptr<BoxStsd> stsd;
    std::shared_ptr<BoxStts> stts;
    std::shared_ptr<BoxCtts> ctts;
    std::shared_ptr<BoxStss> stss;
    
    uint32_t stszSampleSizeIndex = 0;
    uint32_t stscEntryIndex = 0;
    uint32_t entrySampleIndex = 0;
    uint32_t chunkLogicIndex = 0;
    uint32_t sampleOffset = 0;
    
    uint32_t sttsEntryIndex = 0;
    uint32_t sttsEntrySampleIndex = 0;
    
    uint32_t cttsEntryIndex = 0;
    uint32_t cttsEntrySampleIndex = 0;
    
    uint64_t dts = 0;
    uint64_t pts = 0;
    uint64_t index = 0;
    uint64_t keyIndex = 0;
    uint16_t naluByteSize = 0;
public:
    bool isInRange(Buffer& buffer) const noexcept;
    
    bool isInRange(Buffer& buffer, uint64_t& offset) const noexcept;
    
    void reset() noexcept;
    
    void seek(uint64_t pos) noexcept;
    
    uint64_t getSeekPos(long double time) const noexcept;
    
    void parseData(Buffer& buffer,
                   std::shared_ptr<FrameInfo> frameInfo,
                   AVFramePtrArray& packets);
    
    AVFramePtrArray praseH264FrameData(AVFramePtr ptr,
                                    DataPtr data,
                                    std::shared_ptr<VideoFrameInfo>frameInfo);
    void calcIndex() noexcept;
};

class Mp4Demuxer: public IDemuxer {
public:
    Mp4Demuxer() {
        type_ = DemuxerType::MP4;
    }

    ~Mp4Demuxer() override;
    
    bool open(std::unique_ptr<Buffer>& buffer) noexcept override;

    void close() noexcept override;
    
    void seekPos(uint64_t pos) noexcept override;

    DemuxerResult parseData(DataPacket& packet) noexcept override;
    
    [[nodiscard]] uint64_t getSeekToPos(long double time) noexcept override;

    inline static const DemuxerInfo& info() noexcept {
        static DemuxerInfo info = {
            DemuxerType::MP4,
            "ftyp",
            BaseClass<Mp4Demuxer>::registerClass(GetClassName(Mp4Demuxer))
        };
        return info;
    }
    
    bool probeMoovBox(Buffer& buffer, int64_t& start, uint32_t& size) noexcept;
    std::string description() const noexcept;
private:
    bool parseMoovBox(Buffer& buffer, const BoxRefPtr& moovBox) noexcept;
    void initData() noexcept;
private:
    BoxRefPtr rootBox_;
    std::unordered_map<CodecId, std::shared_ptr<TrackContext>> tracks_;
};

}
