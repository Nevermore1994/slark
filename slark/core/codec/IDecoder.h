//
//  Decoder.hpp
//  slark
//
//  Created by Nevermore on 2022/4/23.
//
#pragma once
#include "Thread.h"
#include "RingBuffer.hpp"
#include "NonCopyable.h"
#include "AVFrameDeque.hpp"
#include <memory>

namespace slark {

enum class DecoderType {
    Unknown = 0,
    AAC = 1000,
    RAW = 1001,
    AudioDecoderEnd,
    H264 = 2000,
    VideoDecoderEnd,
};

struct DecoderInfo {
    DecoderType type = DecoderType::Unknown;
    std::string decoderName;
};

class IDecoder : public NonCopyable {
public:
    ~IDecoder() override = default;

public:
    virtual void open() noexcept = 0;

    virtual void reset() noexcept = 0;

    virtual void close() noexcept = 0;

    DecoderType type() const noexcept {
        return decoderType_;
    }

    virtual AVFrameList decode(AVFrameList&& frameList) = 0;

protected:
    DecoderType decoderType_ = DecoderType::Unknown;
    AVFrameSafeDeque deque_;
};

}

