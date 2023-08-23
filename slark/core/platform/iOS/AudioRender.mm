//
//  AudioRender.m
//  slark
//
//  Created by Nevermore on 2022/8/13.
//

#include "AudioDescription.h"
#include "AudioRender.h"
#include "Log.hpp"
#include <algorithm>

namespace slark::Audio {

using namespace slark;

std::unique_ptr<IAudioRender> createAudioRender(std::shared_ptr<AudioInfo> audioInfo) {
    SAssert(audioInfo != nullptr, "audio render info is nullptr");
    return std::make_unique<AudioRender>(audioInfo);
}

bool checkOSStatus(OSStatus status, const char *errorMsg)
{
    if (status != noErr) {
        LogE("Error happen in %s, and status = %x\n", errorMsg, status);
        return false;
    }
    return true;
}

static OSStatus AudioRenderCallback(void *inRefCon,
                                    AudioUnitRenderActionFlags *ioActionFlags,
                                    const AudioTimeStamp* /*inTimeStamp*/,
                                    UInt32 /*inBusNumber*/,
                                    UInt32 /*inNumberFrames*/,
                                    AudioBufferList *__nullable ioData)
{
    @autoreleasepool {
        auto render = static_cast<AudioRender*>(inRefCon);
        
        auto silenceHandler = [&]() {
            for (decltype(ioData->mNumberBuffers) i = 0; i < ioData->mNumberBuffers; i++) {
                std::fill(reinterpret_cast<uint8_t*>(ioData->mBuffers[i].mData), reinterpret_cast<uint8_t*>(ioData->mBuffers[i].mData) + ioData->mBuffers[i].mDataByteSize, 0);
            };
        };
        if (render->status() != AudioRenderStatus::Play) {
            *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            silenceHandler();
            return noErr;
        }
        
        if (!render->requestNextAudioData){
            LogE("render request data function is nullptr.");
            *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            silenceHandler();
            return noErr;
        }
        
        if (!render->isNeedRequestData()) {
            *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            silenceHandler();
            LogI("now no need data, status:%d", render->status());
            return noErr;
        }
        
        for (decltype(ioData->mNumberBuffers) i = 0; i < ioData->mNumberBuffers; i++) {
            auto data = render->requestNextAudioData(ioData->mBuffers[i].mDataByteSize);
            if (!data->empty()) {
                std::copy(reinterpret_cast<uint8_t*>(ioData->mBuffers[i].mData), reinterpret_cast<uint8_t*>(ioData->mBuffers[i].mData) + ioData->mBuffers[i].mDataByteSize, data->rawData);
            } else {
                *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
                silenceHandler();
            }
        }
        return noErr;
    }
}

AudioRender::AudioRender(std::shared_ptr<AudioInfo> audioInfo)
    : IAudioRender(audioInfo) {
    auto isSuccess = checkFormat();
    if (isSuccess) {
        setupAUGraph();
    }
    status_ = isSuccess ? AudioRenderStatus::Ready : AudioRenderStatus::Error;
}

AudioRender::~AudioRender() {
    stop();
}

void AudioRender::play() noexcept {
    if (status_ == AudioRenderStatus::Play || status_ == AudioRenderStatus::Stop) {
        LogI("[audio render] start play return:%d", status_); 
        return;
    }
    status_ = AudioRenderStatus::Play;
    LogI("[audio render] play.");
    checkOSStatus(AUGraphStart(auGraph_), "start AUGraph error.");
}

void AudioRender::pause() noexcept {
    if (status_ == AudioRenderStatus::Pause || status_ == AudioRenderStatus::Stop) {
        LogI("[audio render] pause return:%d", status_);
    }
    status_ = AudioRenderStatus::Pause;
    LogI("[audio render] pause.");
    checkOSStatus(AUGraphStop(auGraph_), "pause stop AUGraph error.");
}

void AudioRender::flush() noexcept {
    checkOSStatus(AudioUnitReset(volumeUnit_, kAudioUnitScope_Global, kAudioUnitOutputBus), "flush error.");
}

void AudioRender::stop() noexcept {
    LogI("[audio render] stop start.");
    if (status_ == AudioRenderStatus::Stop) {
        LogI("[audio render] stop cancel.");
        return;
    }
    status_ = AudioRenderStatus::Stop;
    if (auGraph_) {
        AUGraphStop(auGraph_);
    }
    
    if (renderUnit_) {
        AudioOutputUnitStop(renderUnit_);
        AudioComponentInstanceDispose(renderUnit_);
        renderUnit_ = nullptr;
    }
    
    if (volumeUnit_) {
        AudioOutputUnitStop(volumeUnit_);
        AudioComponentInstanceDispose(volumeUnit_);
        volumeUnit_ = nullptr;
    }
    
    if (auGraph_) {
        AUGraphUninitialize(auGraph_);
        DisposeAUGraph(auGraph_);
        auGraph_ = nullptr;
    }

    LogI("[audio render] stop end.");
}

bool AudioRender::isNeedRequestData() const noexcept {
    if (status_ == AudioRenderStatus::Stop || status_ == AudioRenderStatus::Pause || status_ == AudioRenderStatus::Error) {
        return false;
    }
    return true;
}

bool AudioRender::checkFormat() const noexcept {
    if (audioInfo_ == nullptr) {
        LogE("audio render format is nullptr.");
        return false;
    }
    if (audioInfo_->channels == 0 || audioInfo_->sampleRate == 0.0f || audioInfo_->bitsPerSample == 0) {
        LogE("audio render format is invalid.");
        return false;
    }
    return true;
}

bool AudioRender::setupAUGraph() noexcept {
    auto format = convertInfo2Description(*audioInfo_);
    
    checkOSStatus(NewAUGraph(&auGraph_), "create AUGraph error.");
    checkOSStatus(AUGraphOpen(auGraph_), "open AUGraph error.");
    
    // init render unit
    AudioComponentDescription renderComponentDescription = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_RemoteIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0
    };
    checkOSStatus(AUGraphAddNode(auGraph_, &renderComponentDescription, &renderIndex_), "create render node error.");
    checkOSStatus(AUGraphNodeInfo(auGraph_, renderIndex_, &renderComponentDescription, &renderUnit_), "get render Audio Unit error.");
    
    checkOSStatus(AudioUnitSetProperty(renderUnit_,
                                       kAudioUnitProperty_StreamFormat,
                                       kAudioUnitScope_Input,
                                       kAudioUnitOutputBus,
                                       &format,
                                       sizeof(AudioStreamBasicDescription)),
                  "set audio unit callback error.");
    
    AURenderCallbackStruct renderCallback = {
        .inputProc = AudioRenderCallback,
        .inputProcRefCon = this,
    };
    
    checkOSStatus(AudioUnitSetProperty(renderUnit_,
                                       kAudioUnitProperty_SetRenderCallback,
                                       kAudioUnitScope_Input,
                                       kAudioUnitOutputBus,
                                       &renderCallback,
                                       sizeof(renderCallback)),
                  "set render audio unit callback error.");
    
    UInt32 zero;
    checkOSStatus(AudioUnitSetProperty(renderUnit_,
                                       kAudioOutputUnitProperty_StartTimestampsAtZero,
                                       kAudioUnitScope_Input,
                                       kAudioUnitOutputBus,
                                       &zero,
                                       sizeof(zero)),
                  "set render audio unit start timestamp error.");
    
    //init volume unit
    AudioComponentDescription volumeComponentDescription = {
        .componentType = kAudioUnitType_Mixer,
        .componentSubType = kAudioUnitSubType_MatrixMixer,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0
    };
    checkOSStatus(AUGraphAddNode(auGraph_, &volumeComponentDescription, &volumeIndex_), "create volume audio unit error.");
    
    checkOSStatus(AUGraphNodeInfo(auGraph_, volumeIndex_, &volumeComponentDescription, &volumeUnit_), "get volume audio unit error.");
    checkOSStatus(AudioUnitSetProperty(volumeUnit_,
                                       kAudioUnitProperty_StreamFormat,
                                       kAudioUnitScope_Input,
                                       kAudioUnitOutputBus,
                                       &format,
                                       sizeof(AudioStreamBasicDescription)),
                  "set volume audio unit format error.");
    
    checkOSStatus(AUGraphConnectNodeInput(auGraph_, volumeIndex_, kAudioUnitOutputBus, renderIndex_, kAudioUnitOutputBus), "audio unit connect error.");
    if (!checkOSStatus(AUGraphInitialize(auGraph_),  "audio graph init error.")) {
        return false;
    }
    
    return true;
}

}
