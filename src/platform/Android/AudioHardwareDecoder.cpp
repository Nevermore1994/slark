//
// Created by Nevermore on 2025/3/9.
//

#include "AudioHardwareDecoder.h"
#include "DecoderConfig.h"
#include "NativeHardwareDecoder.h"

namespace slark {

bool AudioHardwareDecoder::decode(slark::AVFramePtr frame) noexcept {
    assert(frame && frame->isAudio());
    auto pts = frame->pts;
    if (decodeFrames_.contains(pts)) {
        LogE("decode same pts frame, {}", pts);
        return false;
    }

    auto data = frame->detachData();
    NativeDecodeFlag flag = NativeDecodeFlag::None;
    if (frame->info->isEndOfStream) {
        flag = NativeDecodeFlag::EndOfStream;
    }
    decodeFrames_[pts] = std::move(frame);
    NativeHardwareDecoder::sendData(decoderId_, std::move(data), pts, flag);
    return true;
}

bool AudioHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config);
    if (!audioConfig) {
        LogE("audio decoder config is invalid");
        return false;
    }
    decoderId_ = NativeHardwareDecoder::createAudio(audioConfig->mediaInfo,
                                                    audioConfig->sampleRate,
                                                    audioConfig->channels,
                                                    audioConfig->profile);
    if (decoderId_.empty()) {
        LogE("create audio decoder failed");
        return false;
    }
    NativeDecoderManager::shareInstance().add(decoderId_, shared_from_this());
    LogI("create audio decoder:{}", decoderId_);
    return true;
}



} // slark