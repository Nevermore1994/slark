//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AudioHardwareDecoder.h"

#include "DecoderConfig.h"
#include "NativeHardwareDecoder.h"

namespace slark {

void AudioHardwareDecoder::flush() noexcept {
    NativeDecoder::flush();
    isCompleted_ = false;
    pendingEOSFrame_.reset();
}

Error AudioHardwareDecoder::decode(AVFrameRefPtr& frame) noexcept {
    assert(frame && frame->isAudio());
    NativeDecodeFlag flag = NativeDecodeFlag::None;
    if (frame->info->isEndOfStream) {
        flag = NativeDecodeFlag::EndOfStream;
    }
    auto res = NativeHardwareDecoder::sendPacket(
        decoderId_, frame->data,
        int64_t(frame->ptsTime() * 1000000),  // convert to microseconds
        flag, PacketType::Audio);
    if (res != Error::Success && res != Error::CodecAgain && res != Error::CodecNotProcessed) {
        LogE("decode frame error:{}", static_cast<int>(res));
    } else {
        LogIThrottle(2000, "send audio packet success, pts time:{:.3f}, isEOS:{}",
                     frame->ptsTime(), frame->info->isEndOfStream);
    }
    // EOS 成功入队：立即标记 completed，不依赖 native 回调
    if (frame->info->isEndOfStream && res == Error::Success) {
        isCompleted_ = true;
        LogI("audio EOS sent, mark completed");
    }
    // EOS 被 codec 拒绝（输入队列满）时缓存，等 receiveDecodedData 回调后重试
    if (frame->info->isEndOfStream && res == Error::CodecNotProcessed) {
        pendingEOSFrame_ = frame;
    }
    return res;
}

void AudioHardwareDecoder::retryPendingEOS() noexcept {
    if (!pendingEOSFrame_) {
        return;
    }
    if (!pendingEOSFrame_->info || !pendingEOSFrame_->info->isEndOfStream) {
        pendingEOSFrame_.reset();
        return;
    }
    auto res = NativeHardwareDecoder::sendPacket(decoderId_, pendingEOSFrame_->data,
                                                 int64_t(pendingEOSFrame_->ptsTime() * 1000000),
                                                 NativeDecodeFlag::EndOfStream, PacketType::Audio);
    if (res == Error::Success) {
        isCompleted_ = true;
        LogI("pending audio EOS retry success, mark completed");
        pendingEOSFrame_.reset();
    }
    // CodecNotProcessed / CodecAgain → 保持 pendingEOSFrame_，下次回调后继续重试
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
    NativeDecoderManager::shareInstance().add(decoderId_, shared_from_this());
    LogI("create audio decoder:{}", decoderId_);
    isOpen_ = true;
    return true;
}

void AudioHardwareDecoder::receiveDecodedData(DataPtr data,
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
    frame->info = std::move(audioFrameInfo);
    invokeReceiveFunc(DecoderData{std::move(frame), Error::Success, currentSeekEpoch_});
    retryPendingEOS();
}

}  // namespace slark