//
//  AudioDescription.mm
//  slark
//
//  Created by 谭平 on 2022/8/22.
//

#import "AudioDescription.h"

using namespace slark::Audio;

AudioStreamBasicDescription convertInfo2Description(slark::AudioInfo info) {
    AudioStreamBasicDescription description;
    description.mFormatID = kAudioFormatLinearPCM;
    description.mSampleRate = info.sampleRate;
    description.mChannelsPerFrame = info.channels;
    description.mFramesPerPacket = 1;
    description.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    description.mBitsPerChannel = info.bitsPerSample * 8;
    description.mBytesPerFrame = info.bitsPerSample * description.mChannelsPerFrame;
    description.mBytesPerPacket = description.mBytesPerFrame * description.mFramesPerPacket;
    return description;
}
