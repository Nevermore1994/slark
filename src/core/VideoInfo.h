//
//  VideoInfo.hpp
//  slark
//
//  Created by Nevermore.
//

#pragma once

#include "Time.hpp"

namespace slark {

struct VideoInfo {
    uint32_t width{};
    uint32_t height{};
    uint16_t fps{};
    CTime startTime;
    uint32_t timeScale{};
    std::string_view mediaInfo;
};

} //end namespace slark

