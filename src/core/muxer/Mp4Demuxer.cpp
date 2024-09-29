//
//  Mp4Demuxer.cpp
//  slark
//
//  Created by Nevermore
//

#include "Mp4Demuxer.hpp"

namespace slark {


std::tuple<bool, uint64_t> Mp4Demuxer::open(std::string_view probeData) noexcept {
    return {false, 0};
}

void Mp4Demuxer::close() noexcept {
    
}

DemuxerResult Mp4Demuxer::parseData(std::unique_ptr<Data> data, int64_t offset) noexcept {
    return {};
}

uint64_t Mp4Demuxer::getSeekToPos(Time::TimePoint) noexcept {
    return 0;
}

}

