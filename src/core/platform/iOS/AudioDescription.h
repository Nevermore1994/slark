//
//  AudioDescription.h
//  slark
//
//  Created by Nevermore on 2022/8/22.
//

#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include "AudioDefine.h"
#include "DecoderConfig.h"

namespace slark {

static constexpr int32_t kAudioUnitOutputBus = 0;
static constexpr int32_t kAudioUnitInputBus = 1;

AudioStreamBasicDescription convertInfo2Description(const AudioInfo& info);

AudioStreamBasicDescription convertConfigOutputDescription(const AudioDecoderConfig& config);
}

