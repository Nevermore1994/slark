//
//  AudioDescription.h
//  slark
//
//  Created by Nevermore on 2022/8/22.
//

#pragma once

#include "AudioInfo.h"
#include "DecoderConfig.h"

#include <AudioToolbox/AudioToolbox.h>

namespace slark {

AudioStreamBasicDescription convertInfo2Description(const AudioInfo& info);

AudioStreamBasicDescription convertConfigOutputDescription(const AudioDecoderConfig& config);
} // namespace slark
