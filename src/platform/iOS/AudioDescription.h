//
//  AudioDescription.h
//  slark
//
//  Created by Nevermore on 2022/8/22.
//

#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include "AudioInfo.h"
#include "DecoderConfig.h"

namespace slark {

AudioStreamBasicDescription convertInfo2Description(const AudioInfo& info);

AudioStreamBasicDescription convertConfigOutputDescription(const AudioDecoderConfig& config);
}

