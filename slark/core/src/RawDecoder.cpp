//
//  rawDecoder.cpp
//  slark
//
//  Created by 谭平 on 2022/8/7.
//

#include "rawDecoder.hpp"

using namespace slark;

void RawDecoder::open() noexcept {

}

void RawDecoder::reset() noexcept {
    deque_.clear();
}

void RawDecoder::close() noexcept {
    deque_.clear();
}

AVFrameList RawDecoder::decode(AVFrameList &&frameList){
    return std::move(frameList);
}
