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
    return true;
}

void RawDecoder::reset() noexcept {
    isOpen_ = false;
}

void RawDecoder::close() noexcept {
    reset();
}

DecoderErrorCode RawDecoder::decode(AVFrameRefPtr& frame) noexcept {
    invokeReceiveFunc(std::move(frame));
    return DecoderErrorCode::Success;
}

void RawDecoder::flush() noexcept {

}


}//end namespace slark
