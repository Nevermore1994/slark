//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DecoderManager.h"

#include "MediaDefs.h"
#include "RawDecoder.h"
#if SLARK_IOS
#include "AACHardwareDecoder.h"
#include "VideoHardwareDecoder.h"
#elif SLARK_ANDROID
#include "AudioHardwareDecoder.h"
#include "VideoHardwareDecoder.h"
#endif

namespace slark {

constexpr std::string_view kSoftwareFlag = "SW";
constexpr std::string_view kHardwareFlag = "HW";

DecoderManager& DecoderManager::shareInstance() noexcept {
    static DecoderManager instance;
    return instance;
}

DecoderManager::DecoderManager() noexcept {
    init();
}

void DecoderManager::init() noexcept {
    mediaInfo_ = {
        {std::string(MEDIA_MIMETYPE_AUDIO_RAW), DecoderType::RAW},
        {std::string(MEDIA_MIMETYPE_AUDIO_AAC) + std::string(kHardwareFlag),
         DecoderType::AACHardwareDecoder},
        {std::string(MEDIA_MIMETYPE_VIDEO_AVC) + std::string(kHardwareFlag),
         DecoderType::VideoHardwareDecoder},
        {std::string(MEDIA_MIMETYPE_VIDEO_HEVC) + std::string(kHardwareFlag),
         DecoderType::VideoHardwareDecoder},
    };

    decoderInfo_ = {
        {DecoderType::RAW, RawDecoder::info()},
#if SLARK_IOS
        {DecoderType::VideoHardwareDecoder, VideoHardwareDecoder::info()},
        {DecoderType::AACHardwareDecoder, AACHardwareDecoder::info()},
#elif SLARK_ANDROID
        {DecoderType::VideoHardwareDecoder, VideoHardwareDecoder::info()},
        {DecoderType::AACHardwareDecoder, AudioHardwareDecoder::info()},
#endif
    };
}

DecoderType DecoderManager::getDecoderType(std::string_view mediaInfo,
                                           bool isSoftDecode) const noexcept {
    if (MEDIA_MIMETYPE_AUDIO_RAW == mediaInfo) {
        return DecoderType::RAW;
    }
    auto name = std::string(mediaInfo).append(isSoftDecode ? kSoftwareFlag : kHardwareFlag);
    if (auto it = mediaInfo_.find(name); it != mediaInfo_.end()) {
        return it->second;
    }
    return DecoderType::Unknown;
}

std::shared_ptr<IDecoder> DecoderManager::create(DecoderType type) const noexcept {
    auto it = decoderInfo_.find(type);
    if (it == decoderInfo_.end()) {
        return nullptr;
    }
    return Reflection::shareInstance().create<IDecoder>(it->second.decoderName);
}

bool DecoderManager::contains(DecoderType type) const noexcept {
    return decoderInfo_.contains(type);
}

}  // namespace slark
