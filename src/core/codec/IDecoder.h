//
//  IDecoder.h
//  slark
//
//  Created by Nevermore.
//
#pragma once
#include "NonCopyable.h"
#include "AVFrame.hpp"
#include "Reflection.hpp"

namespace slark {

enum class DecoderType {
    Unknown = 0,
    RAW = 1000,
    AACSoftwareDecoder = 1001,
    AACHardwareDecoder = 1002,
    AudioDecoderEnd,
    VideoHardWareDecoder = 2001,
};

struct DecoderTypeInfo {
    DecoderType type = DecoderType::Unknown;
    std::string decoderName;
};

struct DecoderConfig {
    uint32_t timeScale{};
    virtual ~DecoderConfig() = default;
};

using DecoderReceiveFunc = std::function<void(AVFramePtr)>;

class DecoderDataProvider {
public:
    virtual ~DecoderDataProvider() = default;
    
    virtual AVFramePtr getDecodeFrame() noexcept = 0;
};

class IDecoder : public NonCopyable {
public:
    ~IDecoder() override = default;
public:
    [[nodiscard]] DecoderType type() const noexcept {
        return decoderType_;
    }

    [[nodiscard]] bool isAudio() const noexcept {
        return DecoderType::Unknown < decoderType_ && decoderType_ < DecoderType::AudioDecoderEnd;
    }

    [[nodiscard]] bool isVideo() const noexcept {
        return DecoderType::AudioDecoderEnd < decoderType_;
    }

    [[nodiscard]] bool isOpen() const noexcept {
        return isOpen_;
    }
    
    [[nodiscard]] bool isFlushed() const noexcept {
        return isFlushed_;
    }
    
    void setProvider(std::weak_ptr<DecoderDataProvider> provider) noexcept {
        provider_ = provider;
    }
    
    virtual bool send(AVFramePtr frame) = 0;

    virtual void flush() noexcept = 0;
    
    virtual void reset() noexcept = 0;
    
    virtual bool open(std::shared_ptr<DecoderConfig> config) noexcept = 0;
    
    virtual void close() noexcept = 0;
    
public:
    DecoderReceiveFunc receiveFunc;
protected:
    bool isOpen_ = false;
    bool isFlushed_ = false;
    DecoderType decoderType_ = DecoderType::Unknown;
    std::shared_ptr<DecoderConfig> config_;
    std::weak_ptr<DecoderDataProvider> provider_;
};

}

