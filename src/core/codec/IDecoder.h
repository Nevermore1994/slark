//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once
#include <string>
#include <string_view>
#include <utility>

#include "AVFrame.hpp"
#include "Error.h"
#include "NonCopyable.h"
#include "Reflection.hpp"
#include "Synchronized.hpp"

namespace slark {

enum class DecoderType {
    Unknown = 0,
    RAW = 1000,
    AACSoftwareDecoder = 1001,
    AACHardwareDecoder = 1002,
    AudioDecoderEnd,
    VideoHardwareDecoder = 2001,
};

[[nodiscard]] inline constexpr std::string_view decoderTypeName(DecoderType type) noexcept {
    switch (type) {
        case DecoderType::RAW:
            return "RAW PCM (passthrough)";
        case DecoderType::AACSoftwareDecoder:
            return "AAC (software)";
        case DecoderType::AACHardwareDecoder:
#if defined(__ANDROID__)
            return "AAC (MediaCodec hardware)";
#elif defined(__APPLE__)
            return "AAC (AudioToolbox hardware)";
#else
            return "AAC (hardware)";
#endif
        case DecoderType::VideoHardwareDecoder:
#if defined(__ANDROID__)
            return "Video (MediaCodec hardware)";
#elif defined(__APPLE__)
            return "Video (VideoToolbox hardware)";
#else
            return "Video (hardware)";
#endif
        default:
            return "n/a";
    }
}

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
    uint64_t seekEpoch{0};  ///< seek epoch captured at dispatch, used for stale detection in callbacks
    int32_t origin = 0;
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

    [[nodiscard]] virtual std::string name() const noexcept {
        return std::string(decoderTypeName(decoderType_));
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

    virtual bool hasPendingOutput() const noexcept {
        return false;
    }

    virtual Error renderOutput() noexcept {
        return Error::CodecError;
    }

    virtual void clearPendingOutput() noexcept {}

    using WakeupCallback = std::function<void()>;

    void setReceiveFunc(DecoderReceiveFunc&& func) noexcept {
        receiveFunc_.reset(std::make_shared<DecoderReceiveFunc>(std::move(func)));
    }

    void setWakeupCallback(WakeupCallback cb) noexcept {
        if (cb) {
            wakeupCallback_.reset(std::make_shared<WakeupCallback>(std::move(cb)));
        } else {
            wakeupCallback_.reset();
        }
    }

    bool isCompleted() const noexcept {
        return isCompleted_;
    }

    void invokeReceiveFunc(DecoderData data) noexcept {
        if (auto func = receiveFunc_.load()) {
            (*func)(std::move(data));
        }
    }

    void notifyWakeup() noexcept {
        if (auto callback = wakeupCallback_.load()) {
            (*callback)();
        }
    }

    void setCurrentSeekEpoch(uint64_t epoch) noexcept {
        currentSeekEpoch_ = epoch;
    }

    [[nodiscard]] uint64_t currentSeekEpoch() const noexcept {
        return currentSeekEpoch_;
    }

 protected:
    std::atomic_bool isOpen_ = false;
    std::atomic_bool isCompleted_ = false;
    AtomicSharedPtr<DecoderReceiveFunc> receiveFunc_;
    AtomicSharedPtr<WakeupCallback> wakeupCallback_;
    DecoderType decoderType_ = DecoderType::Unknown;
    std::shared_ptr<DecoderConfig> config_;
    uint64_t currentSeekEpoch_ = 0;
};

}  // namespace slark

#define SLARK_DECODER_INFO(cls, decoderType)                 \
    static constexpr DecoderType kDecoderType = decoderType; \
    static constexpr std::string_view kDecoderName = #cls;
