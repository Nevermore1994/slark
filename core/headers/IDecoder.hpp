//
//  Decoder.hpp
//  slark
//
//  Created by Nevermore on 2022/4/23.
//
#pragma once
#include "Thread.hpp"
#include "RingBuffer.hpp"
#include "NonCopyable.hpp"
#include <memory>

namespace slark {

enum class DecoderType {
    Unknown = 0,
    AAC = 1000,
    AudioDecoderEnd,
    H264 = 2000,
    VideoDecoderEnd,
};

struct DecoderInfo{
    DecoderType type = DecoderType::Unknown;
    std::string decoderName;
};

class IDecoder : public slark::NonCopyable {
public:
    ~IDecoder() override = default;

public:
    virtual void open() noexcept = 0;
    
    virtual void reset() noexcept = 0;
    
    virtual void close() noexcept = 0;
    
    DecoderType type() const noexcept {
        return decoderType_;
    }

private:
    DecoderType decoderType_ = DecoderType::Unknown;
};

}

