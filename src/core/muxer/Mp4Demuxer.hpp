//
//  Mp4Demuxer.hpp
//  slark
//
//  Created by Nevermore
//

#pragma once

#include "IDemuxer.h"
#include "Mp4Box.hpp"

namespace slark {

class TrackContext {
public:
    std::shared_ptr<BoxMdhd> mdhd;
    std::shared_ptr<Box> stbl;
    std::shared_ptr<BoxStco> stco;
    std::shared_ptr<BoxStsz> stsz;
    std::shared_ptr<BoxStsc> stsc;
    std::shared_ptr<BoxStsd> stsd;
    std::shared_ptr<BoxStts> stts;
};

class Mp4Demuxer: public IDemuxer {
public:
    Mp4Demuxer() {
        type_ = DemuxerType::MP4;
    }

    ~Mp4Demuxer() override = default;

    bool open(std::unique_ptr<Buffer>& buffer) noexcept override;

    void close() noexcept override;

    DemuxerResult parseData(std::unique_ptr<Data> data, int64_t offset) noexcept override;
    
    [[nodiscard]] uint64_t getSeekToPos(Time::TimePoint) noexcept override;

    inline static const DemuxerInfo& info() noexcept {
        static DemuxerInfo info = {
            DemuxerType::MP4,
            "ftyp",
            BaseClass<Mp4Demuxer>::registerClass(GetClassName(Mp4Demuxer))
        };
        return info;
    }
    
    bool probeMoovBox(Buffer& buffer, int64_t& start, uint32_t& size) noexcept;
private:
    bool parseMoovBox(Buffer& buffer, BoxRefPtr moovBox) noexcept;
private:
    BoxRefPtr rootBox_;
    std::unordered_map<CodecId, std::shared_ptr<TrackContext>> tracks_;
};

}
