//
// Created by Nevermore on 2025/3/6.
//

#include "VideoHardwareDecoder.h"
#include "NativeHardwareDecoder.h"
#include "DecoderConfig.h"

namespace slark {

bool VideoHardwareDecoder::decode(AVFramePtr frame) noexcept {
    assert(frame && frame->isVideo());
    auto pts = frame->pts;
    if (decodeFrames_.contains(pts)) {
        LogE("decode same pts frame, {}", pts);
        return false;
    }

    auto data = frame->detachData();
    NativeDecodeFlag flag = NativeDecodeFlag::None;
    auto frameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frame->info);
    if (frameInfo->isIDRFrame) {
        flag = NativeDecodeFlag::KeyFrame;
    } else if (frameInfo->isEndOfStream){
        flag = NativeDecodeFlag::EndOfStream;
    }
    decodeFrames_[pts] = std::move(frame);
    NativeHardwareDecoder::sendData(decoderId_, std::move(data), pts, flag);
    return true;
}

bool VideoHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    auto videoConfig = std::dynamic_pointer_cast<VideoDecoderConfig>(config);
    if (!videoConfig) {
        LogE("video decoder config is invalid");
        return false;
    }
    decoderId_ = NativeHardwareDecoder::createVideo(videoConfig->mediaInfo,
                                               static_cast<int32_t>(videoConfig->width),
                                               static_cast<int32_t>(videoConfig->height));
    if (decoderId_.empty()) {
        LogE("create video decoder failed");
        return false;
    }
    NativeDecoderManager::shareInstance().add(decoderId_, shared_from_this());
    LogI("create video decoder:{}", decoderId_);
    return true;
}



} // slark