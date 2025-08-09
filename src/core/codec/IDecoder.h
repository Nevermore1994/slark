//
//  IDecoder.h
//  slark
//
//  Created by Nevermore.
//
#pragma once
#include <utility>
#include <condition_variable>
#include "NonCopyable.h"
#include "AVFrame.hpp"
#include "Reflection.hpp"
#include "Synchronized.hpp"

namespace slark {

enum class DecoderType {
    Unknown = 0,
    RAW = 1000,
    AACSoftwareDecoder = 1001,
    AACHardwareDecoder = 1002,
    AudioDecoderEnd,
    VideoHardWareDecoder = 2001,
};

enum class DecoderErrorCode: int8_t {
    Unknown = 0,
    Again = 1,
    Success = 2,
    NotProcessed = -1,
    NotStart = -2,
    InputDataError = -3,
    InputDataTooLarge = -4,
    NotFoundDecoder = -5,
    ErrorDecoder = -6,
};

struct DecoderTypeInfo {
    DecoderType type = DecoderType::Unknown;
    std::string decoderName;
};

struct DecoderConfig {
    std::string playerId;
    uint32_t timeScale{};
    std::string mediaInfo;
    uint8_t profile{};
    uint8_t level{};
    virtual ~DecoderConfig() = default;
};

class DecoderDataProvider {
public:
    virtual ~DecoderDataProvider() = default;

    virtual AVFrameRefPtr requestDecodeFrame(bool isBlocking) noexcept = 0;
};

using DecoderReceiveFunc = std::function<void(AVFrameRefPtr)>;

class IDecoder : public NonCopyable {
public:
    explicit IDecoder(DecoderType type)
        : decoderType_(type) {

    }
    ~IDecoder() override {
        setReceiveFunc(nullptr);
    }
public:
    [[nodiscard]] DecoderType type() const noexcept {
        return decoderType_;
    }

    [[nodiscard]] bool isAudio() const noexcept {
        return isAudioDecoder(decoderType_);
    }

    [[nodiscard]] bool isVideo() const noexcept {
        return isVideoDecoder(decoderType_);
    }

    static bool isAudioDecoder(DecoderType type) noexcept {
        return DecoderType::Unknown < type && type < DecoderType::AudioDecoderEnd;
    }

    static bool isVideoDecoder(DecoderType type) noexcept {
        return DecoderType::AudioDecoderEnd < type;
    }

    [[nodiscard]] bool isOpen() const noexcept {
        return isOpen_;
    }

    AVFrameRefPtr requestDecodeFrame(bool isBlocking = true) noexcept {
        return provider_->requestDecodeFrame(isBlocking);
    }

    virtual void flush() noexcept = 0;
    
    virtual void reset() noexcept {
        flush();
        isCompleted_ = false;
    }
    
    virtual bool open(std::shared_ptr<DecoderConfig> config) noexcept = 0;
    
    virtual void close() noexcept {
        reset();
    }

    virtual DecoderErrorCode decode(AVFrameRefPtr& frame) noexcept = 0;

    void setReceiveFunc(DecoderReceiveFunc&& func) noexcept {
        receiveFunc_.reset(std::make_shared<DecoderReceiveFunc>(std::move(func)));
    }

    void setDataProvider(std::shared_ptr<DecoderDataProvider> provider) noexcept {
        provider_ = std::move(provider);
    }

    bool isCompleted() const noexcept {
        return isCompleted_ ;
    }
    
    void invokeReceiveFunc(AVFrameRefPtr frame) noexcept {
        if (auto func = receiveFunc_.load()) {
            (*func)(std::move(frame));
        }
    }
protected:
    std::atomic_bool isOpen_ = false;
    std::atomic_bool isCompleted_ = false;
    std::shared_ptr<DecoderDataProvider> provider_;
    AtomicSharedPtr<DecoderReceiveFunc> receiveFunc_;
    DecoderType decoderType_ = DecoderType::Unknown;
    std::shared_ptr<DecoderConfig> config_;
};

}

