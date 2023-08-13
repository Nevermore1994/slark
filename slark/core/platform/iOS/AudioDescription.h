//
//  AudioDescription.h
//  slark
//
//  Created by Nevermore on 2022/8/22.
//

#pragma once

#import <AudioToolbox/AudioToolbox.h>
#include "AudioInfo.h"

namespace slark::Audio {

static constexpr int32_t kAudioUnitOutputBus = 0;
static constexpr int32_t kAudioUnitInputBus = 1;

AudioStreamBasicDescription convertInfo2Description(slark::AudioInfo info);


}

