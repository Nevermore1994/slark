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
    isOpen_ = true;
}

void RawDecoder::reset() noexcept {
    isOpen_ = false;
    isFlushed_ = false;
}

void RawDecoder::close() noexcept {
    isOpen_ = false;
    isFlushed_ = false;
}

AVFrameArray RawDecoder::send(AVFrameArray frameList) {
    for (auto& frame : frameList) {
        frame->stats.decodedStamp = Time::nowTimeStamp();
    }
    return std::move(frameList);
}

AVFrameArray RawDecoder::flush() noexcept {
    isFlushed_ = true;
    return {};
}


}//end namespace slark
