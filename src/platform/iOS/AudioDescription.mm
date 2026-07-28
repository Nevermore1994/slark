
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "AudioDescription.h"

using namespace slark;

namespace slark {

AudioStreamBasicDescription convertInfo2Description(const AudioInfo& info) {
    AudioStreamBasicDescription description;
    description.mFormatID = kAudioFormatLinearPCM;
    description.mSampleRate = static_cast<Float64>(info.sampleRate);
    description.mChannelsPerFrame = info.channels;
    description.mFramesPerPacket = 1;
    description.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    description.mBitsPerChannel = info.bitsPerSample;
    description.mBytesPerFrame = description.mBitsPerChannel * description.mChannelsPerFrame / 8;
    description.mBytesPerPacket = description.mBytesPerFrame * description.mFramesPerPacket;
    description.mReserved = 0;
    return description;
}

}  // namespace slark
