//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "DecoderConfig.h"
#include "Log.hpp"
#include "NativeDecoderManager.h"
#include "NativeHardwareDecoder.h"

namespace slark {

// Common audio hardware decoder implementation: open()/receiveDecodedData() are provided
// by the base class; subclasses only implement decode()/flush() and optional data-ready
// hooks (e.g. EOS retry).
template <typename Derived>
class AudioHardwareDecoderBase : public NativeDecoder,
                                 public std::enable_shared_from_this<Derived> {
 public:
    explicit AudioHardwareDecoderBase(DecoderType type) : NativeDecoder(type) {}

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override;

    void receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept override;

 protected:
    virtual void onDecodedDataReady() noexcept {}
};

template <typename Derived>
bool AudioHardwareDecoderBase<Derived>::open(std::shared_ptr<DecoderConfig> config) noexcept {
    config_ = std::move(config);
    auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config_);
    if (!audioConfig) {
        LogE("audio decoder config is invalid");
        return false;
    }
    decoderId_ = NativeHardwareDecoder::createAudioDecoder(audioConfig);
    if (decoderId_.empty()) {
        LogE("create audio decoder failed");
        return false;
    }
    NativeDecoderManager::shareInstance().add(decoderId_, this->shared_from_this());
    LogI("create audio decoder:{}", decoderId_);
    isOpen_ = true;
    return true;
}

template <typename Derived>
void AudioHardwareDecoderBase<Derived>::receiveDecodedData(DataPtr data,
                                                           int64_t pts,
                                                           bool isCompleted) noexcept {
    isCompleted_ = isCompleted;
    if (isCompleted) {
        LogI("audio decode completed");
    }
    if (!data || data->empty()) {
        if (!isCompleted) {
            LogE("data is empty");
        }
        return;
    }
    auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config_);
    if (!audioConfig) {
        LogE("audio decoder config is invalid");
        return;
    }
    auto frame = std::make_unique<AVFrame>(AVFrameType::Audio);
    frame->pts = pts;
    frame->dts = pts;
    frame->timeScale = 1000000;  // us
    frame->data = std::move(data);
    auto audioFrameInfo = std::make_shared<AudioFrameInfo>();
    audioFrameInfo->sampleRate = audioConfig->sampleRate;
    audioFrameInfo->channels = audioConfig->channels;
    audioFrameInfo->bitsPerSample = audioConfig->bitsPerSample;
    frame->info = audioFrameInfo;
    invokeReceiveFunc(DecoderData{std::move(frame), Error::Success, currentSeekEpoch_});
    onDecodedDataReady();
}

}  // namespace slark
