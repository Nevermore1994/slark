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
        LogIThrottle(2000, "send audio packet success, pts time:{:.3f}, isEOS:{}", frame->ptsTime(),
                     frame->info->isEndOfStream);
    }
    // EOS queued successfully: mark completed immediately without relying on a native callback
    if (frame->info->isEndOfStream && res == Error::Success) {
        isCompleted_ = true;
        LogI("audio EOS sent, mark completed");
    }
    // Cache EOS rejected by the codec (full input queue); retry after receiveDecodedData
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
    // CodecNotProcessed / CodecAgain -> keep pendingEOSFrame_ and retry on the next callback
}

void AudioHardwareDecoder::onDecodedDataReady() noexcept {
    retryPendingEOS();
}

}  // namespace slark
