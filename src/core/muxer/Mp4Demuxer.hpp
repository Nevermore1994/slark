//
//  Mp4Demuxer.hpp
//  slark
//
//  Created by Nevermore
//

#pragma once

#include "IDemuxer.h"
#include <string_view>

namespace slark {

class Mp4Demuxer: public IDemuxer {
public:
    Mp4Demuxer();

    ~Mp4Demuxer() override = default;

    std::tuple<bool, uint64_t> open(std::string_view probeData) noexcept override;

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

};

}
