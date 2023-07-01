//
//  VideoInfo.hpp
//  Slark
//
//  Created by Nevermore on 2022/8/22.
//

#pragma once

#include "Time.hpp"

namespace Slark {

struct VideoInfo {
    uint32_t width{};
    uint32_t height{};
    uint32_t fps{};
    CTime offset;
    CTime duration;
};

} //end namespace Slark

