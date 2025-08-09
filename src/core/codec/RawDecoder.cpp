//
//  rawDecoder.cpp
//  slark
//
//  Created by Nevermore on 2022/8/7.
//

#include "RawDecoder.h"

namespace slark {

bool RawDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    config_ = config;
    isOpen_ = true;
    isCompleted_ = false;
    return true;
}

void RawDecoder::reset() noexcept {
    isOpen_ = false;
    isCompleted_ = false;
}

void RawDecoder::close() noexcept {
    reset();
}

DecoderErrorCode RawDecoder::decode(AVFrameRefPtr& frame) noexcept {
    if (!frame->info->isEndOfStream) {
        invokeReceiveFunc(std::move(frame));
    } else {
        isCompleted_ = true;
    }
    return DecoderErrorCode::Success;
}

void RawDecoder::flush() noexcept {
    isCompleted_ = false;
}


}//end namespace slark
