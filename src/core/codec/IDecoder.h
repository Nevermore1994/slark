//
//  Decoder.hpp
//  slark
//
//  Created by Nevermore.
//
#pragma once
#include "NonCopyable.h"
#include "AVFrame.hpp"
#include "Synchronized.hpp"
#include "Reflection.hpp"
#include <string_view>

namespace slark {

enum class DecoderType {
    Unknown = 0,
    RAW = 1000,
    AACSoftwareDecoder = 1001, //ffmpeg
    AACHardwareDecoder = 1002, //hardware
    AudioDecoderEnd,
    H264SoftWareDecoder = 2000, //ffmpeg
    H264HardWareDecoder = 2001,//hardware
    VideoDecoderEnd,
};

struct DecoderInfo {
    DecoderType type = DecoderType::Unknown;
    std::string_view mediaInfo;
    std::string decoderName;
};

using DecoderReceiveFunc = std::function<void(AVFrameArray)>;

class IDecoder : public NonCopyable {
public:

    ~IDecoder() override = default;
public:
    virtual void open() noexcept = 0;

    virtual void reset() noexcept = 0;

    virtual void close() noexcept = 0;

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

    [[nodiscard]] inline bool isFlushed() const noexcept {
        return isFlushed_;
    }
    
    virtual AVFrameArray send(AVFrameArray frameList) = 0;

    virtual AVFrameArray flush() noexcept = 0;
protected:
    bool isOpen_ = false;
    bool isFlushed_ = false;
    DecoderType decoderType_ = DecoderType::Unknown;
};

}

