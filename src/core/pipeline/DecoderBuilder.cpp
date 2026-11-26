//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "DecoderBuilder.h"

#include <utility>

#include "DecoderConfig.h"
#include "DecoderManager.h"

namespace slark::engine {

DecoderBuilder::DecoderBuilder(PlayerSetting setting, PlayerContext context, Override override)
    : setting_(std::move(setting)),
      context_(std::move(context)),
      override_(std::move(override)) {}

std::shared_ptr<slark::IDecoder> DecoderBuilder::createAudio(
    const std::shared_ptr<slark::AudioInfo>& info) const {
    if (override_) {
        return override_(true);
    }
    if (!info || info->sampleRate == 0 || info->channels == 0) {
        return nullptr;
    }
    auto& manager = DecoderManager::shareInstance();
    auto decoder = manager.create(manager.getDecoderType(info->mediaInfo,
                                                         setting_.enableAudioSoftDecode));
    if (!decoder) {
        return nullptr;
    }
    auto config = std::make_shared<slark::AudioDecoderConfig>();
    config->initWithAudioInfo(info);
    config->playerId = context_.playerId;
    return decoder->open(config) ? decoder : nullptr;
}

std::shared_ptr<slark::IDecoder> DecoderBuilder::createVideo(
    const std::shared_ptr<slark::VideoInfo>& info) const {
    if (override_) {
        return override_(false);
    }
    auto& manager = DecoderManager::shareInstance();
    auto decoder = manager.create(manager.getDecoderType(info->mediaInfo,
                                                         setting_.enableVideoSoftDecode));
    if (!decoder) {
        return nullptr;
    }
    auto config = std::make_shared<slark::VideoDecoderConfig>();
    if (!config->initWithVideoInfo(info)) {
        return nullptr;
    }
    config->playerId = context_.playerId;
    return decoder->open(config) ? decoder : nullptr;
}

}  // namespace slark::engine
