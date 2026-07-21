//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AACHardwareDecoder.h"

#include "DecoderConfig.h"
#include "NativeHardwareDecoder.h"

namespace slark {

void AACHardwareDecoder::flush() noexcept {
    NativeDecoder::flush();
    isCompleted_ = false;
}

Error AACHardwareDecoder::decode(AVFrameRefPtr& frame) noexcept {
    assert(frame && frame->isAudio());
    NativeDecodeFlag flag = NativeDecodeFlag::None;
    if (frame->isEOS) {
        flag = NativeDecodeFlag::EndOfStream;
    }
    auto res = NativeHardwareDecoder::sendPacket(
        decoderId_,
        frame->data,
        int64_t(frame->ptsTime() * 1000000), // convert to microseconds
        flag,
        PacketType::Audio);
    if (res == Error::Success || res == Error::CodecAgain) {
        LogI("send audio packet success, pts:{}, dts:{}, pts time:{}, dts time:{}, isEOS:{}",
             frame->pts,
             frame->dts,
             frame->ptsTime(),
             frame->dtsTime(),
             frame->isEOS);
    } else {
        LogE("decode frame error:{}", toString(res));
    }
    return res;
}

bool AACHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
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
    NativeDecoderManager::shareInstance().add(decoderId_, shared_from_this());
    LogI("create audio decoder:{}", decoderId_);
    isOpen_ = true;
    return true;
}

void AACHardwareDecoder::receiveDecodedData(DataPtr data, int64_t pts, bool isCompleted) noexcept {
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
    frame->timeScale = 1000000; // us
    frame->data = std::move(data);
    auto audioInfo = std::make_shared<AudioFrameInfo>();
    audioInfo->sampleRate = static_cast<uint32_t>(audioConfig->sampleRate);
    audioInfo->channels = audioConfig->channels;
    audioInfo->bitsPerSample = audioConfig->bitsPerSample;
    frame->info = audioInfo;
    invokeReceiveFunc(DecoderData{std::move(frame), Error::Success, currentSeekEpoch_});
}

} // namespace slark
