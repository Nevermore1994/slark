//
//  rawDecoder.cpp
//  slark
//
//  Created by Nevermore on 2022/8/7.
//

#include "RawDecoder.h"
#include "Time.hpp"

namespace slark {

bool RawDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    isOpen_ = true;
    return true;
}

void RawDecoder::reset() noexcept {
    isOpen_ = false;
    isFlushed_ = false;
}

void RawDecoder::close() noexcept {
    reset();
}

bool RawDecoder::send(AVFramePtr frame) {
    frame->stats.decodedStamp = Time::nowTimeStamp();
    if (receiveFunc) {
        receiveFunc(std::move(frame));
    }
    return true;
}

void RawDecoder::flush() noexcept {
    isFlushed_ = true;
}


}//end namespace slark
