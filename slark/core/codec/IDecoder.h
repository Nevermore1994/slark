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
#include "AVFrame.hpp"
#include <memory>

namespace slark {

enum class DecoderType {
    Unknown = 0,
    RAW = 1000,
    AACSoftwareDecoder = 1001,
    AACHardwareDecoder = 1002,
    AudioDecoderEnd,
    H264SoftWareDecoder = 2000,
    H264HardWareDecoder = 2001,
    VideoDecoderEnd,
};

struct DecoderInfo {
    DecoderType type = DecoderType::Unknown;
    std::string decoderName;
};

using DecoderReceiveCallback = std::function<void(AVFrameArray)>;

class IDecoder : public NonCopyable {
public:

    ~IDecoder() override = default;
public:
    virtual void open() noexcept = 0;

    virtual void reset() noexcept = 0;

    virtual void close() noexcept = 0;

    [[nodiscard]] inline DecoderType subType() const noexcept {
        return decoderSubType_;
    }

    [[nodiscard]] inline DecoderType type() const noexcept {
        return decoderType_;
    }

    [[nodiscard]] inline bool isAudio() const noexcept {
        return DecoderType::Unknown < decoderType_ && decoderType_ < DecoderType::AudioDecoderEnd;
    }

    [[nodiscard]] inline bool isVideo() const noexcept {
        return DecoderType::AudioDecoderEnd < decoderType_ && decoderType_ < DecoderType::VideoDecoderEnd;
    }

    [[nodiscard]] inline bool isOpen() const noexcept {
        return isOpen_;
    }

    virtual AVFrameArray send(AVFrameArray&& frameList) = 0;

    virtual AVFrameArray flush() noexcept = 0;
protected:
    DecoderType decoderType_;
    DecoderType decoderSubType_ = DecoderType::Unknown;
    bool isOpen_;
    AVFrameSafeDeque deque_;
};

}

