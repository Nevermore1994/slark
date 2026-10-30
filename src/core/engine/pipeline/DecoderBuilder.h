//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <functional>
#include <memory>

#include "AudioInfo.h"
#include "IDecoder.h"
#include "PlayerContext.h"
#include "PlayerTypes.h"
#include "VideoInfo.h"

namespace slark::engine {

// Both methods return null when no decoder is available or it cannot be opened.
class DecoderBuilder {
 public:
    // `override` replaces the platform decoders wholesale (test fakes open themselves).
    using Override = std::function<std::shared_ptr<slark::IDecoder>(bool isAudio)>;

    DecoderBuilder(PlayerSetting setting, PlayerContext context, Override override = {});

    [[nodiscard]] std::shared_ptr<slark::IDecoder> createAudio(
        const std::shared_ptr<slark::AudioInfo>& info) const;
    [[nodiscard]] std::shared_ptr<slark::IDecoder> createVideo(
        const std::shared_ptr<slark::VideoInfo>& info) const;

 private:
    PlayerSetting setting_;
    PlayerContext context_;
    Override override_;
};

}  // namespace slark::engine
