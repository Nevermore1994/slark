//
//  DecoderManager.cpp
//  slark
//
//  Created by Nevermore on 2022/4/24.
//

#include "DecoderManager.h"
#include "MediaDefs.h"
#include "RawDecoder.h"
#include <string>

namespace slark {

constexpr std::string_view kSoftwareFlag = "SW";
constexpr std::string_view kHardwareFlag = "HW";

void DecoderManager::init() noexcept {
    mediaInfo_ = {
        {std::string(MEDIA_MIMETYPE_AUDIO_RAW), DecoderType::RAW},
        {std::string(MEDIA_MIMETYPE_AUDIO_AAC) + std::string(kSoftwareFlag), DecoderType::AACSoftwareDecoder},
        {std::string(MEDIA_MIMETYPE_AUDIO_AAC) + std::string(kHardwareFlag), DecoderType::AACHardwareDecoder},
        {std::string(MEDIA_MIMETYPE_VIDEO_AVC) + std::string(kSoftwareFlag), DecoderType::H264SoftWareDecoder},
        {std::string(MEDIA_MIMETYPE_VIDEO_AVC) + std::string(kSoftwareFlag), DecoderType::H264HardWareDecoder},
    };

    decoderInfo_ = {
        {DecoderType::RAW, RawDecoder::info()},
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

std::unique_ptr<IDecoder> DecoderManager::create(DecoderType type) const noexcept {
    if (!decoderInfo_.contains(type)) {
        return nullptr;
    }
    auto& decoderInfo = decoderInfo_.at(type);
    return std::unique_ptr<IDecoder>(BaseClass<IDecoder>::create(decoderInfo.decoderName));
}

bool DecoderManager::contains(DecoderType type) const noexcept {
    return decoderInfo_.contains(type);
}


}//end namespace slark
