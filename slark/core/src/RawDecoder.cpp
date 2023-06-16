//
//  rawDecoder.cpp
//  Slark
//
//  Created by Nevermore on 2022/8/7.
//

#include "RawDecoder.hpp"
#include "Time.hpp"

namespace Slark {

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
        frame->decodedStamp = Time::nowTimeStamp();
    }
    return std::move(frameList);
}

}//end namespace Slark
