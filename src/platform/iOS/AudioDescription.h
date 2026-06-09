//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "AudioInfo.h"
#include "DecoderConfig.h"

#include <AudioToolbox/AudioToolbox.h>

namespace slark {

AudioStreamBasicDescription convertInfo2Description(const AudioInfo& info);

AudioStreamBasicDescription convertConfigOutputDescription(const AudioDecoderConfig& config);
} // namespace slark