//
//  VideoInfo.h
//  slark
//
//  Created by Nevermore.
//

#pragma once

#include "Data.hpp"
#include "MediaDefs.h"

namespace slark {

struct VideoInfo {
    uint16_t fps{};
    uint16_t naluHeaderLength;
    uint32_t width{};
    uint32_t height{};
    uint32_t timeScale{};
    std::string_view mediaInfo;
    DataRefPtr sps;
    DataRefPtr pps;
    DataRefPtr vps;
};

} //end namespace slark

