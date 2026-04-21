
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

#import "AudioDescription.h"

using namespace slark;

namespace slark {

AudioStreamBasicDescription convertInfo2Description(const AudioInfo& info) {
    AudioStreamBasicDescription description;
    description.mFormatID = kAudioFormatLinearPCM;
    description.mSampleRate = info.sampleRate;
    description.mChannelsPerFrame = info.channels;
    description.mFramesPerPacket = 1;
    description.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    description.mBitsPerChannel = info.bitsPerSample;
    description.mBytesPerFrame = description.mBitsPerChannel * description.mChannelsPerFrame / 8;
    description.mBytesPerPacket = description.mBytesPerFrame * description.mFramesPerPacket;
    description.mReserved = 0;
    return description;
}

} // namespace slark
