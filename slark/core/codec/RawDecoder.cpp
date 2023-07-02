//
//  rawDecoder.cpp
//  slark
//
//  Created by Nevermore on 2022/8/7.
//

#include "RawDecoder.h"
#include "Time.hpp"

namespace slark {

void RawDecoder::open() noexcept {

}

void RawDecoder::reset() noexcept {
    deque_.clear();
}

void RawDecoder::close() noexcept {
    deque_.clear();
}

AVFrameList RawDecoder::decode(AVFrameList&& frameList) {
    for (auto& frame : frameList) {
        frame->decodedStamp = Time::NowTimeStamp();
    }
    return std::move(frameList);
}

}//end namespace slark
