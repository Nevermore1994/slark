//
//  iOSAACHWDecoder.cpp
//  slark
//
//  Created by Nevermore on 2024/10/25.
//

#include "iOSAACHWDecoder.hpp"
#include "AudioDescription.h"
#include "Log.hpp"

namespace slark {

#define endOfStream -1

OSStatus AACDecodeInputDataProc(AudioConverterRef,
                                UInt32 *ioNumberDataPackets,
                                AudioBufferList *ioData,
                                AudioStreamPacketDescription** outDataPacketDescription,
                                void *inUserData) {
    if (inUserData == nullptr) {
        return endOfStream;
    }
    auto framePtr = reinterpret_cast<AVFrame*>(inUserData);
    auto audioFrameInfo = std::any_cast<AudioFrameInfo>(framePtr->info);
    auto dataPtr = framePtr->detachData();
    ioData->mBuffers[0].mDataByteSize = static_cast<UInt32>(dataPtr->length);
    ioData->mBuffers[0].mNumberChannels = audioFrameInfo.channels;
    std::copy(dataPtr->rawData, dataPtr->rawData + dataPtr->length, static_cast<uint8_t*>(ioData->mBuffers[0].mData));
    *ioNumberDataPackets = 1;
    if (outDataPacketDescription) {
        (*outDataPacketDescription)[0].mStartOffset = 0;
        (*outDataPacketDescription)[0].mVariableFramesInPacket = 1024;
        (*outDataPacketDescription)[0].mDataByteSize = static_cast<UInt32>(dataPtr->length);
    }
    return noErr;
}

iOSAACHWDecoder::~iOSAACHWDecoder() {
    close();
}

void iOSAACHWDecoder::reset() noexcept {
    @autoreleasepool {
        if (decodeSession_) {
            AudioConverterDispose(decodeSession_);
            decodeSession_ = nullptr;
        }
        if (outputData_){
            free(outputData_->mBuffers[0].mData);
            outputData_.reset();
        }
    }
}

bool iOSAACHWDecoder::send(AVFramePtr frame) noexcept {
    auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config_);
    UInt32 outputDataPacketSize = 1024;
    OSStatus status = AudioConverterFillComplexBuffer(decodeSession_,
                                                      AACDecodeInputDataProc,
                                                      reinterpret_cast<void*>(frame.get()),
                                                      &outputDataPacketSize,
                                                      outputData_.get(),
                                                      NULL);
    
    if (status != noErr) {
        LogI("Error during decoding: {}", status);
    } else {
        auto data = reinterpret_cast<uint8_t*>(outputData_->mBuffers[0].mData);
        auto decodeData = std::make_unique<Data>(outputData_->mBuffers[0].mDataByteSize);
        decodeData->length = outputData_->mBuffers[0].mDataByteSize;
        std::copy(data, data + outputData_->mBuffers[0].mDataByteSize, decodeData->rawData);
        frame->data = std::move(decodeData);
        frame->stats.decodedStamp = Time::nowTimeStamp();
        if (receiveFunc) {
            receiveFunc(std::move(frame));
        }
    }
    return true;
}

void iOSAACHWDecoder::flush() noexcept {
    isFlushed_ = true;
}

bool iOSAACHWDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    reset();
    config_ = config;
    auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config);
    if (!audioConfig) {
        LogI("error, isn't audio config");
        return false;
    }
    AudioStreamBasicDescription inputFormat;
    inputFormat.mFormatID = kAudioFormatMPEG4AAC;
    inputFormat.mSampleRate = audioConfig->sampleRate;
    inputFormat.mChannelsPerFrame = audioConfig->channels;
    inputFormat.mFormatFlags = kMPEG4Object_AAC_LC;
    inputFormat.mBitsPerChannel = 0;
    inputFormat.mBytesPerFrame = 0;
    inputFormat.mBytesPerPacket = 0;
    inputFormat.mFramesPerPacket = 1024;
    inputFormat.mReserved = 0;
   
    
    AudioStreamBasicDescription outputFormat;
    outputFormat.mFormatID = kAudioFormatLinearPCM;
    outputFormat.mSampleRate = audioConfig->sampleRate;
    outputFormat.mChannelsPerFrame = audioConfig->channels;
    outputFormat.mFramesPerPacket = 1;
    outputFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    outputFormat.mBitsPerChannel = audioConfig->bitsPerSample;
    outputFormat.mBytesPerFrame = outputFormat.mBitsPerChannel * outputFormat.mChannelsPerFrame / 8;
    outputFormat.mBytesPerPacket = outputFormat.mBytesPerFrame * outputFormat.mFramesPerPacket;
    outputFormat.mReserved = 0;

    
    OSStatus status = AudioConverterNew(&inputFormat, &outputFormat, &decodeSession_);
    if (status != noErr) {
        LogI("Error creating audio converter: {}", status);
        return false;
    }
    auto bytePerFrame = static_cast<uint64_t>(audioConfig->bitsPerSample * audioConfig->channels / 8);
    outputData_ = std::make_unique<AudioBufferList>();
    outputData_->mNumberBuffers = 1;
    outputData_->mBuffers[0].mNumberChannels = audioConfig->channels;
    outputData_->mBuffers[0].mDataByteSize = static_cast<UInt32>(bytePerFrame * 1024);
    outputData_->mBuffers[0].mData = malloc(outputData_->mBuffers[0].mDataByteSize);
    
    return true;
}

void iOSAACHWDecoder::close() noexcept {
    reset();
}

}
