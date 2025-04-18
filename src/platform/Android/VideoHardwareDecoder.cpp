//
// Created by Nevermore on 2025/3/6.
//

#include "VideoHardwareDecoder.h"
#include "NativeHardwareDecoder.h"
#include "DecoderConfig.h"

namespace slark {

DecoderErrorCode VideoHardwareDecoder::decode(AVFrameRefPtr& frame) noexcept {
    assert(frame && frame->isVideo());
    auto pts = frame->pts;
    if (decodeFrames_.contains(pts)) {
        LogE("decode same pts frame, {}", pts);
        return DecoderErrorCode::NotFoundDecoder;
    }

    NativeDecodeFlag flag = NativeDecodeFlag::None;
    auto frameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frame->info);
    if (frameInfo->isIDRFrame) {
        flag = NativeDecodeFlag::KeyFrame;
    } else if (frameInfo->isEndOfStream){
        flag = NativeDecodeFlag::EndOfStream;
    }
    auto res = NativeHardwareDecoder::sendPacket(decoderId_, frame->data, pts, flag);
    if (res == DecoderErrorCode::Success) {
        decodeFrames_[pts] = std::move(frame);
    }
    return res;
}

bool VideoHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config);
    if (!videoConfig) {
        LogE("video decoder config is invalid");
        return false;
    }
    config_ = config;
    decoderId_ = NativeHardwareDecoder::createVideo(videoConfig);
    if (decoderId_.empty()) {
        LogE("create video decoder failed!");
        return false;
    }
    NativeDecoderManager::shareInstance().add(decoderId_, shared_from_this());
    LogI("create video decoder:{}", decoderId_);
    return true;
}

} // slark