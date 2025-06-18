//
// Created by Nevermore on 2025/3/9.
//

#include "AudioHardwareDecoder.h"
#include "DecoderConfig.h"
#include "NativeHardwareDecoder.h"

namespace slark {

void AudioHardwareDecoder::flush() noexcept {
    NativeDecoder::flush();
    isCompleted_ = false;
}

DecoderErrorCode AudioHardwareDecoder::decode(AVFrameRefPtr &frame) noexcept {
    assert(frame && frame->isAudio());
    NativeDecodeFlag flag = NativeDecodeFlag::None;
    if (frame->info->isEndOfStream) {
        flag = NativeDecodeFlag::EndOfStream;
    }
    auto res = NativeHardwareDecoder::sendPacket(
        decoderId_,
        frame->data,
        int64_t(frame->ptsTime() * 1000000), // convert to microseconds
        flag,
        false // false for audio
    );
    if (res == DecoderErrorCode::Success) {
        LogI("send audio packet success, pts:{}, dts:{}, pts time:{}, dts time:{}",
             frame->pts,
             frame->dts,
             frame->ptsTime(),
             frame->dtsTime()
        );
    } else {
        LogE("decode frame error:{}", static_cast<int>(res));
    }
    return res;
}

bool AudioHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
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
    NativeDecoderManager::shareInstance().add(
        decoderId_,
        shared_from_this());
    LogI("create audio decoder:{}",
         decoderId_);
    isOpen_ = true;
    return true;
}

void AudioHardwareDecoder::receiveDecodedData(
    DataPtr data,
    int64_t pts,
    bool isCompleted
) noexcept {
    isCompleted_ = isCompleted;
    if (isCompleted) {
        LogI("audio decode completed");
    }
    if (!data || data->empty()) {
        LogE("data is empty");
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
    frame->timeScale = 1000000; //us
    frame->data = std::move(data);
    auto audioFrameInfo = std::make_shared<AudioFrameInfo>();
    audioFrameInfo->sampleRate = audioConfig->sampleRate;
    audioFrameInfo->channels = audioConfig->channels;
    audioFrameInfo->bitsPerSample = audioConfig->bitsPerSample;
    frame->info = std::move(audioFrameInfo);
    invokeReceiveFunc(std::move(frame));
}


} // slark