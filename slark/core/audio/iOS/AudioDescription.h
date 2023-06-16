//
//  AudioDescription.h
//  slark
//
//  Created by 谭平 on 2022/8/22.
//

#pragma once

#import <AudioToolbox/AudioToolbox.h>
#include "AudioInfo.hpp"

namespace slark::Audio {

AudioStreamBasicDescription convertInfo2Description(slark::AudioInfo info);

}

