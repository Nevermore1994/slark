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
    uint32_t fps{};
    CTime duration;
};

} //end namespace slark

