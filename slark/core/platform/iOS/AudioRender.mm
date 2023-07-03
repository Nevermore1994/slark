//
//  AudioRender.m
//  slark
//
//  Created by Nevermore on 2022/8/13.
//

#import "AudioRender.h"
#include "Log.h"
#include <algorithm>

namespace slark::Audio {

using namespace slark;

bool checkOSStatus(OSStatus status, const char *errorMsg)
{
    if (status != noErr) {
#if DEBUG
    loge("Error happen in %s, and status = %x\n", errorMsg, status);
#endif
        return false;
    }
    return true;
}

static OSStatus AudioRenderCallback(void *inRefCon,
                                    AudioUnitRenderActionFlags *ioActionFlags,
                                    const AudioTimeStamp *inTimeStamp,
                                    UInt32 inBusNumber,
                                    UInt32 inNumberFrames,
                                    AudioBufferList *__nullable ioData)
{
    @autoreleasepool {
        AudioRender* render = (AudioRender *)inRefCon;
        
        if (render->status() != AudioRenderStatus::Play) {
            *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            return noErr;
        }
        
        if(!render->requestNextAudioData){
            *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            return noErr;
        }
        Data data = render->requestNextAudioData(ioData->mBuffers[0].mDataByteSize,inTimeStamp->mSampleTime / render->format().mSampleRate);
        
        if (!data.empty()) {
            std::copy((char*)ioData->mBuffers[0].mData, (char*)ioData->mBuffers[0].mData + ioData->mBuffers[0].mDataByteSize, data.data);
        } else {
            *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
        }
        
        return noErr;
    }
}

AudioRender::AudioRender(std::unique_ptr<AudioForamt> format)
    :format_(std::move(format)) {
    
}

AudioRender::~AudioRender() {
    stop();
    checkOSStatus(AudioUnitUninitialize(audioUnit_), "AudioUnitUninitialize");
    checkOSStatus(AudioComponentInstanceDispose(audioUnit_), "AudioComponentInstanceDispose");
}

void AudioRender::play() noexcept {
    status_ = AudioRenderStatus::Play;
    logi("[audio render] play.");
    checkOSStatus(AudioOutputUnitStart(audioUnit_), "AudioOutputUnitStart");
}

void AudioRender::pause() noexcept {
    status_ = AudioRenderStatus::Pause;
    logi("[audio render] pause.");
}

void AudioRender::stop() noexcept {
    status_ = AudioRenderStatus::Stop;
    logi("[audio render] pause.");
    checkOSStatus(AudioOutputUnitStop(audioUnit_), "AudioOutputUnitStop");
}

bool AudioRender::setupAudioUnit() noexcept {
    OSStatus status;
    
    // init audio unit
    AudioComponentDescription audioComponentDescription = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_RemoteIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0
    };
    AudioComponent inputComponect = AudioComponentFindNext(NULL, &audioComponentDescription);
    status = AudioComponentInstanceNew(inputComponect, &audioUnit_);
    if (!checkOSStatus(status, "AudioComponentInstanceNew") || !audioUnit_) {
        return NO;
    }
        
    checkOSStatus(AudioUnitSetProperty(audioUnit_,
                                       kAudioUnitProperty_StreamFormat,
                                       kAudioUnitScope_Input,
                                       0,
                                       format_.get(),
                                       sizeof(AudioStreamBasicDescription)),
                  "kAudioUnitProperty_StreamFormat");
    
    AURenderCallbackStruct renderCallback = {
        .inputProc = AudioRenderCallback,
        .inputProcRefCon = this,
    };
    
    checkOSStatus(AudioUnitSetProperty(audioUnit_,
                                       kAudioUnitProperty_SetRenderCallback,
                                       kAudioUnitScope_Input,
                                       0,
                                       &renderCallback,
                                       sizeof(renderCallback)),
                  "kAudioUnitProperty_SetRenderCallback");
    
    UInt32 zero;
    checkOSStatus(AudioUnitSetProperty(audioUnit_,
                                       kAudioOutputUnitProperty_StartTimestampsAtZero,
                                       kAudioUnitScope_Input,
                                       0,
                                       &zero,
                                       sizeof(zero)),
                  "kAudioOutputUnitProperty_StartTimestampsAtZero");
    
    status = AudioUnitInitialize(audioUnit_);
    if (!checkOSStatus(status, "AudioUnitInitialize")) {
        return false;
    }
    
    return true;
}

}
