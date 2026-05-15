//
//  IDecoder.h
//  slark
//
//  Created by Nevermore.
//
#pragma once
#include "AVFrame.hpp"
#include "Error.h"
#include "NonCopyable.h"
#include "Reflection.hpp"
#include "Synchronized.hpp"

#include <utility>

namespace slark {

enum class DecoderType {
    Unknown = 0,
    RAW = 1000,
    AACSoftwareDecoder = 1001,
    AACHardwareDecoder = 1002,
    AudioDecoderEnd,
    VideoHardwareDecoder = 2001,
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

struct DecoderData {
    AVFrameRefPtr frame;
    Error error = Error::Success;
};

using DecoderReceiveFunc = std::function<void(DecoderData)>;

class IDecoder : public NonCopyMovable {
public:
    explicit IDecoder(DecoderType type) : decoderType_(type) {}

    virtual ~IDecoder() {
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

    virtual void flush() noexcept = 0;

    virtual void reset() noexcept {
        flush();
        isCompleted_ = false;
    }

    virtual bool open(std::shared_ptr<DecoderConfig> config) noexcept = 0;

    virtual void close() noexcept {
        reset();
    }

    virtual Error decode(AVFrameRefPtr& frame) noexcept = 0;

    void setReceiveFunc(DecoderReceiveFunc&& func) noexcept {
        receiveFunc_.reset(std::make_shared<DecoderReceiveFunc>(std::move(func)));
    }

    bool isCompleted() const noexcept {
        return isCompleted_;
    }

    void invokeReceiveFunc(DecoderData data) noexcept {
        if (auto func = receiveFunc_.load()) {
            (*func)(std::move(data));
        }
    }

protected:
    std::atomic_bool isOpen_ = false;
    std::atomic_bool isCompleted_ = false;
    AtomicSharedPtr<DecoderReceiveFunc> receiveFunc_;
    DecoderType decoderType_ = DecoderType::Unknown;
    std::shared_ptr<DecoderConfig> config_;
};

} // namespace slark

// 为 decoder 子类提供静态类型信息，避免重复实现 type() 方法
#define SLARK_DECODER_INFO(cls, decoderType)                 \
    static constexpr DecoderType kDecoderType = decoderType; \
    static constexpr std::string_view kDecoderName = #cls;
