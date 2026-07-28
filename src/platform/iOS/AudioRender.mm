
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AudioRender.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include "AudioDescription.h"
#include "Base.h"
#include "Log.hpp"
#include "MediaDefs.h"
#include "TimerManager.h"

#define VOLUME_UNIT_INPUT_BUS0 0

static constexpr AudioUnitElement kAudioRemoteIOUnitOutputBus = 0;

static constexpr AudioUnitElement kMixerInputBus = 0;
static constexpr AudioUnitElement kMixerOutputBus = 0;

namespace slark {

using namespace slark;

std::shared_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& audioInfo) {
    SAssert(audioInfo != nullptr, "[audio render] audio render info is nullptr");
    return std::make_unique<AudioRender>(audioInfo);
}

bool checkOSStatus(OSStatus status, const char* errorMsg) {
    if (status != noErr) {
        NSError* error = [NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil];
        LogE("[audio render] Error happen in {}, and status = {}, info:{}\n",
             errorMsg,
             status,
             [[error description] UTF8String]);
        return false;
    }
    return true;
}

static OSStatus AudioRenderCallback(void* inRefCon,
                                    AudioUnitRenderActionFlags* ioActionFlags,
                                    const AudioTimeStamp* /*inTimeStamp*/,
                                    UInt32 /*inBusNumber*/,
                                    UInt32 /*inNumberFrames*/,
                                    AudioBufferList* __nullable ioData) {
    @autoreleasepool {
        auto render = static_cast<AudioRender*>(inRefCon);

        auto silenceHandler = [&]() {
            *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            for (decltype(ioData->mNumberBuffers) i = 0; i < ioData->mNumberBuffers; i++) {
                std::fill(reinterpret_cast<uint8_t*>(ioData->mBuffers[i].mData),
                          reinterpret_cast<uint8_t*>(ioData->mBuffers[i].mData) +
                              ioData->mBuffers[i].mDataByteSize,
                          0);
            };
            return noErr;
        };
        if (render->status() != RenderStatus::Playing) {
            return silenceHandler();
        }

        if (!render->isNeedRequestData()) {
            LogI("[audio render] now no need data, status:{}",
                 static_cast<uint32_t>(render->status()));
            return silenceHandler();
        }

        for (decltype(ioData->mNumberBuffers) i = 0; i < ioData->mNumberBuffers; i++) {
            auto dataPtr = reinterpret_cast<uint8_t*>(ioData->mBuffers[i].mData);
            auto needSize = ioData->mBuffers[i].mDataByteSize;
            if (dataPtr == nullptr || needSize == 0) {
                continue;
            }
            AudioDataFlag flag = AudioDataFlag::Normal;
            uint32_t size = render->requestAudioData(dataPtr, needSize, flag);
            if (size < needSize) {
                std::fill_n(dataPtr + size, needSize - size, 0);
            }
        }
        return noErr;
    }
}

Time::TimeDelta AudioRender::getHardwareLatency() noexcept {
    return Time::TimeDelta(latencyUs_.load(std::memory_order_acquire));
}

AudioRender::AudioRender(std::shared_ptr<AudioInfo> audioInfo) : IAudioRender(audioInfo) {
    auto isSuccess = checkFormat();
    if (isSuccess) {
        setupAudioComponent();
    }
    using namespace std::chrono_literals;
    timerId_ = TimerManager::shareInstance().runLoop(200ms, [this]() {
        if (status_ != RenderStatus::Playing) {
            return;
        }
        Float64 volLatency = 0.0, renderLatency = 0.0;
        UInt32 size = sizeof(Float64);
        AudioUnitGetProperty(renderUnit_,
                             kAudioUnitProperty_Latency,
                             kAudioUnitScope_Global,
                             0,
                             &renderLatency,
                             &size);
        AudioUnitGetProperty(volumeUnit_,
                             kAudioUnitProperty_Latency,
                             kAudioUnitScope_Global,
                             0,
                             &volLatency,
                             &size);
        // 补充 AVAudioSession 硬件 I/O buffer 延迟，获取完整 pipeline 延迟
        AVAudioSession* session = [AVAudioSession sharedInstance];
        const Float64 hwLatency = session.outputLatency + session.ioBufferDuration;
        auto us = static_cast<int64_t>((volLatency + renderLatency + hwLatency) * 1'000'000.0);
        latencyUs_.store(us, std::memory_order_release);
    });
    status_ = isSuccess ? RenderStatus::Ready : RenderStatus::Error;
}

AudioRender::~AudioRender() {
    TimerManager::shareInstance().cancel(timerId_);
    stop();
}

void AudioRender::play() noexcept {
    if (!isNormal()) {
        LogI("[audio render] in error status.");
        return;
    }
    if (status_ == RenderStatus::Playing || status_ == RenderStatus::Stop) {
        LogI("[audio render] start play return:{}", static_cast<uint32_t>(status_.load()));
        return;
    }
    // MultiChannelMixer 由 RemoteIO 驱动，只需 start RemoteIO
    AudioOutputUnitStart(renderUnit_);
    status_ = RenderStatus::Playing;
    LogI("[audio render] play.");
}

void AudioRender::pause() noexcept {
    if (!isNormal()) {
        LogI("in error status.");
        return;
    }
    if (status_ == RenderStatus::Pause || status_ == RenderStatus::Stop) {
        LogI("[audio render] pause return:{}", static_cast<uint32_t>(status_.load()));
        return;
    }
    // MultiChannelMixer 由 RemoteIO 驱动，只需 stop RemoteIO
    AudioOutputUnitStop(renderUnit_);
    status_ = RenderStatus::Pause;
    LogI("[audio render] pause.");
}

void AudioRender::flush() noexcept {
    if (!isNormal()) {
        LogE("[audio render] in error status.");
        return;
    }
    if (status_ == RenderStatus::Stop) {
        LogI("[audio render] flush return:{}", static_cast<uint32_t>(status_.load()));
        return;
    }
    checkOSStatus(AudioUnitReset(volumeUnit_, kAudioUnitScope_Global, kMixerOutputBus),
                  "flush error.");
}

void AudioRender::renderEnd() noexcept {
    AudioRender::pause();
}

void AudioRender::stop() noexcept {
    LogI("[audio render] stop start.");
    if (status_ == RenderStatus::Stop) {
        LogI("[audio render] stoped.");
        return;
    }
    status_ = RenderStatus::Stop;

    // 先停止 RemoteIO，再按 renderUnit -> volumeUnit 顺序 dispose
    if (renderUnit_) {
        AudioOutputUnitStop(renderUnit_);
        AudioComponentInstanceDispose(renderUnit_);
        renderUnit_ = nullptr;
    }

    if (volumeUnit_) {
        AudioUnitUninitialize(volumeUnit_);
        AudioComponentInstanceDispose(volumeUnit_);
        volumeUnit_ = nullptr;
    }
    LogI("[audio render] stop end.");
}

void AudioRender::reset() noexcept {
    // 平台层无需重置时钟，时钟由 AudioRenderComponent 管理
}

void AudioRender::setVolume(float volume) noexcept {
    if (std::fabs(volume - volume_) < std::numeric_limits<float>::epsilon()) {
        return;
    }
    volume_ = volume;
    AudioUnitSetParameter(volumeUnit_,
                          kMultiChannelMixerParam_Volume,
                          kAudioUnitScope_Input,
                          0,
                          volume,
                          0);
    LogI("set volume:{}", volume);
}

bool AudioRender::isNeedRequestData() const noexcept {
    if (status_ == RenderStatus::Stop || status_ == RenderStatus::Pause ||
        status_ == RenderStatus::Error) {
        LogI("audio render not need data.");
        return false;
    }
    return true;
}

bool AudioRender::checkFormat() const noexcept {
    if (audioInfo_ == nullptr) {
        LogE("[audio render] audio render format is nullptr.");
        return false;
    }
    if (audioInfo_->channels == 0 || audioInfo_->sampleRate == 0.0f ||
        audioInfo_->bitsPerSample == 0) {
        LogE("[audio render] audio render format is invalid.");
        return false;
    }
    return true;
}

bool AudioRender::setupAudioComponent() noexcept {
    AudioComponentDescription volumeAudioDesc = {
        .componentType = kAudioUnitType_Mixer,
        .componentSubType = kAudioUnitSubType_MultiChannelMixer,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0,
    };

    auto volumeComponent = AudioComponentFindNext(nullptr, &volumeAudioDesc);
    if (!volumeComponent || !checkOSStatus(AudioComponentInstanceNew(volumeComponent, &volumeUnit_),
                                           "create volume unit error")) {
        return false;
    }

    AudioComponentDescription renderDesc = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_RemoteIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0,
    };

    auto renderComponent = AudioComponentFindNext(nullptr, &renderDesc);
    if (!renderComponent || !checkOSStatus(AudioComponentInstanceNew(renderComponent, &renderUnit_),
                                           "create render component error")) {
        return false;
    }
    auto format = convertInfo2Description(*audioInfo_);

    // Set the format on the input scope of the output element / bus.
    if (!checkOSStatus(AudioUnitSetProperty(renderUnit_,
                                            kAudioUnitProperty_StreamFormat,
                                            kAudioUnitScope_Input,
                                            kAudioRemoteIOUnitOutputBus,
                                            &format,
                                            sizeof(format)),
                       "set Property_StreamFormat on outputbus : input scope")) {
        return false;
    }
    AudioUnitConnection connection;
    connection.sourceAudioUnit = volumeUnit_;
    connection.sourceOutputNumber = kMixerOutputBus;
    connection.destInputNumber = kAudioRemoteIOUnitOutputBus;
    if (!checkOSStatus(AudioUnitSetProperty(renderUnit_,
                                            kAudioUnitProperty_MakeConnection,
                                            kAudioUnitScope_Input,
                                            kAudioRemoteIOUnitOutputBus,
                                            &connection,
                                            sizeof(connection)),
                       "volume unit connect render unit error")) {
        return false;
    }

    if (!checkOSStatus(AudioUnitSetProperty(volumeUnit_,
                                            kAudioUnitProperty_StreamFormat,
                                            kAudioUnitScope_Input,
                                            kMixerOutputBus,
                                            &format,
                                            sizeof(format)),
                       "set volume input format fail")) {
        return false;
    }
    AURenderCallbackStruct callback;
    callback.inputProc = AudioRenderCallback;
    callback.inputProcRefCon = this;
    if (!checkOSStatus(AudioUnitSetProperty(volumeUnit_,
                                            kAudioUnitProperty_SetRenderCallback,
                                            kAudioUnitScope_Input,
                                            kMixerInputBus,
                                            &callback,
                                            sizeof(AURenderCallbackStruct)),
                       "add data callback fail")) {
        return false;
    }
    if (!checkOSStatus(AudioUnitSetProperty(volumeUnit_,
                                            kAudioUnitProperty_StreamFormat,
                                            kAudioUnitScope_Input,
                                            kMixerInputBus,
                                            &format,
                                            sizeof(format)),
                       "set input format error")) {
        return false;
    }

    return checkOSStatus(AudioUnitInitialize(volumeUnit_), "init volume audio unit error") &&
           checkOSStatus(AudioUnitInitialize(renderUnit_), "init render audio unit error");
}

}  // namespace slark
