//
//  AudioInfo.hpp
//  Slark
//
//  Created by Nevermore on 2022/8/22.
//

#pragma once
#include <string>
#include "Time.hpp"

namespace Slark {

struct AudioInfo {
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint64_t sampleRate = 0;
    uint64_t headerLength = 0;
    CTime offset;
    CTime duration;
    std::string format;
};

}
