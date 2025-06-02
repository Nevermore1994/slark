//
// Created by Nevermore on 2025/3/9.
//

#include "AudioHardwareDecoder.h"
#include "DecoderConfig.h"
#include "NativeHardwareDecoder.h"

namespace slark {

DecoderErrorCode AudioHardwareDecoder::decode(AVFrameRefPtr &frame) noexcept {
    assert(frame && frame->isAudio());
    auto pts = frame->pts;
    if (decodeFrames_.contains(pts)) {
        LogE("decode same pts frame, {}",
             pts);
        return DecoderErrorCode::InputDataError;
    }

    NativeDecodeFlag flag = NativeDecodeFlag::None;
    if (frame->info
        ->isEndOfStream) {
        flag = NativeDecodeFlag::EndOfStream;
    }
    auto res = NativeHardwareDecoder::sendPacket(
        decoderId_,
        frame->data,
        pts,
        flag
    );
    if (res == DecoderErrorCode::Success) {
        LogE("send audio packet success, pts:{}, dts:{}",
             frame->pts,
             frame->dts
        );
    } else {
        LogE("decode frame error:{}", static_cast<int>(res));
    }
    return res;
}

bool AudioHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config);
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

void AudioHardwareDecoder::decodeComplete(DataPtr data, int64_t pts) noexcept {
    if (!data || data->empty()) {
        LogE("data is empty");
        return;
    }
    auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config_);
    auto frame = std::make_unique<AVFrame>(AVFrameType::Audio);
    frame->pts = pts;
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