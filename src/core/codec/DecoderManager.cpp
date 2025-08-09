//
//  DecoderManager.cpp
//  slark
//
//  Created by Nevermore on 2022/4/24.
//

#include "DecoderManager.h"
#include "MediaDefs.h"
#include "RawDecoder.h"
#if SLARK_IOS
#include "iOSVideoHWDecoder.h"
#include "iOSAACHWDecoder.h"
#elif SLARK_ANDROID
#include "VideoHardwareDecoder.h"
#include "AudioHardwareDecoder.h"
#endif

namespace slark {

constexpr std::string_view kSoftwareFlag = "SW";
constexpr std::string_view kHardwareFlag = "HW";

DecoderManager& DecoderManager::shareInstance() {
    static DecoderManager instance;
    return instance;
}

DecoderManager::DecoderManager()  {
    init();
}

void DecoderManager::init() noexcept {
    mediaInfo_ = {
        {std::string(MEDIA_MIMETYPE_AUDIO_RAW), DecoderType::RAW},
        {std::string(MEDIA_MIMETYPE_AUDIO_AAC) + std::string(kSoftwareFlag), DecoderType::AACSoftwareDecoder},
        {std::string(MEDIA_MIMETYPE_AUDIO_AAC) + std::string(kHardwareFlag), DecoderType::AACHardwareDecoder},
        {std::string(MEDIA_MIMETYPE_VIDEO_AVC) + std::string(kHardwareFlag), DecoderType::VideoHardWareDecoder},
        {std::string(MEDIA_MIMETYPE_VIDEO_HEVC) + std::string(kHardwareFlag), DecoderType::VideoHardWareDecoder},
    };

    decoderInfo_ = {
        {DecoderType::RAW, RawDecoder::info()},
#if SLARK_IOS
        {DecoderType::VideoHardWareDecoder, iOSVideoHWDecoder::info()},
        {DecoderType::AACHardwareDecoder, iOSAACHWDecoder::info()},
#elif SLARK_ANDROID
        {DecoderType::VideoHardWareDecoder, VideoHardwareDecoder::info()},
        {DecoderType::AACHardwareDecoder, AudioHardwareDecoder::info()},
#endif
    };
}

DecoderType DecoderManager::getDecoderType(std::string_view mediaInfo, bool isSoftDecode) const {
    if (MEDIA_MIMETYPE_AUDIO_RAW == mediaInfo) {
        return DecoderType::RAW;
    }
    auto name = std::string(mediaInfo).append(isSoftDecode ? kSoftwareFlag : kHardwareFlag);
    if (mediaInfo_.contains(name)) {
        return mediaInfo_.at(name);
    }
    return DecoderType::Unknown;
}

std::shared_ptr<IDecoder> DecoderManager::create(DecoderType type) const noexcept {
    if (!decoderInfo_.contains(type)) {
        return nullptr;
    }
    auto& decoderInfo = decoderInfo_.at(type);
    return BaseClass::create<IDecoder>(decoderInfo.decoderName);
}

bool DecoderManager::contains(DecoderType type) const noexcept {
    return decoderInfo_.contains(type);
}


}//end namespace slark
