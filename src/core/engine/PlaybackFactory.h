//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "ComponentFactories.h"
#include "Command.h"
#include "MediaInfo.h"
#include "Pipeline.h"
#include "PlayerContext.h"
#include "PlayerTypes.h"
#include "SeekEpoch.h"
#include "Source.h"

namespace slark::engine {

struct PlaybackFactory {
    using Wake = std::function<void()>;

    std::function<std::unique_ptr<Source>(const Prepare& prepare, SeekEpoch epoch, Wake wake)>
        createSource;
    std::function<std::vector<std::unique_ptr<Pipeline>>(Source& source,
                                                         const MediaInfo& info,
                                                         SeekEpoch epoch,
                                                         Wake wake)>
        createPipelines;
};

[[nodiscard]] PlaybackFactory makePlaybackFactory(PlayerSetting setting,
                                                  PlayerContext context,
                                                  ComponentFactories components);

}  // namespace slark::engine
