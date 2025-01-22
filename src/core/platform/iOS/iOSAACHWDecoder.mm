//
//  iOSAACHWDecoder.cpp
//  slark
//
//  Created by Nevermore on 2024/10/25.
//

#include "iOSAACHWDecoder.hpp"
#include "AudioDescription.h"
#include "Log.hpp"
#include "Util.hpp"

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
    auto decoder = reinterpret_cast<iOSAACHWDecoder*>(inUserData);
    auto framePtr = decoder->getDecodeFrame();
    if (!framePtr || !decoder->isOpen()) {
        decoder->wait();
        framePtr = decoder->getDecodeFrame();
    }
    if (!framePtr) {
        LogI("exit aac decode");
        return endOfStream;
    }
    
    auto dataPtr = framePtr->detachData();
    if (dataPtr == nullptr) {
        LogE("decode audio data is nullptr");
        return noErr;
    }
    auto audioFrameInfo = std::dynamic_pointer_cast<AudioFrameInfo>(framePtr->info);
    ioData->mBuffers[0].mDataByteSize = static_cast<UInt32>(dataPtr->length);
    ioData->mBuffers[0].mNumberChannels = audioFrameInfo->channels;
    std::copy(dataPtr->rawData, dataPtr->rawData + dataPtr->length, static_cast<uint8_t*>(ioData->mBuffers[0].mData));
    *ioNumberDataPackets = 1;
    if (outDataPacketDescription) {
        (*outDataPacketDescription)[0].mStartOffset = 0;
        (*outDataPacketDescription)[0].mVariableFramesInPacket = 1024;
        (*outDataPacketDescription)[0].mDataByteSize = static_cast<UInt32>(dataPtr->length);
    }
    decoder->setDecodeFrame(std::move(framePtr));
    return noErr;
}

iOSAACHWDecoder::iOSAACHWDecoder()
    : worker_(Util::genRandomName("iOSAACDecodeThread"), &iOSAACHWDecoder::decode, this){
    decoderType_ = DecoderType::AACHardwareDecoder;
}

iOSAACHWDecoder::~iOSAACHWDecoder() {
    close();
}

void iOSAACHWDecoder::reset() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isOpen_ = false;
    }
    cond_.notify_all();
    worker_.pause();
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

void iOSAACHWDecoder::decode() noexcept {
    bool isNeedDecode = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pendingFrames_.empty()) {
            isNeedDecode = true;
        }
    }
    if (!isNeedDecode) {
        worker_.pause();
        LogI("decode queue empty!");
        return;
    }

    UInt32 outputDataPacketSize = 1024;
    OSStatus status = AudioConverterFillComplexBuffer(decodeSession_,
                                                      AACDecodeInputDataProc,
                                                      reinterpret_cast<void*>(this),
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
        if (!decodeAVFrame) {
            LogE("decode error, current decode frame is nullptr");
            return;
        }
        decodeAVFrame->data = std::move(decodeData);
        decodeAVFrame->stats.decodedStamp = Time::nowTimeStamp();
        if (receiveFunc) {
            receiveFunc(std::move(decodeAVFrame));
        }
    }
}

bool iOSAACHWDecoder::send(AVFramePtr frame) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!isOpen_) {
            return false;
        }
        pendingFrames_.push_back(std::move(frame));
    }
    worker_.start();
    cond_.notify_all();
    return true;
}

void iOSAACHWDecoder::flush() noexcept {
    isFlushed_ = true;
}

MPEG4ObjectID getAACProfile(uint8_t profile) {
    switch (profile) {
        case 0:
            return kMPEG4Object_AAC_Main;
        case 1:
            return kMPEG4Object_AAC_LC;
        case 2:
            return kMPEG4Object_AAC_SSR;
        case 3:
            return kMPEG4Object_AAC_LTP;
        default:
            return kMPEG4Object_AAC_LC;
    }
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
    inputFormat.mFormatFlags = getAACProfile(audioConfig->profile);
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
    outputFormat.mBitsPerChannel = audioConfig->bitsPerSample == 0 ? 16 : audioConfig->bitsPerSample;
    outputFormat.mBytesPerFrame = outputFormat.mBitsPerChannel * outputFormat.mChannelsPerFrame / 8;
    outputFormat.mBytesPerPacket = outputFormat.mBytesPerFrame * outputFormat.mFramesPerPacket;
    outputFormat.mReserved = 0;

    
    OSStatus status = AudioConverterNew(&inputFormat, &outputFormat, &decodeSession_);
    if (status != noErr) {
        LogI("Error creating audio converter: {}", status);
        return false;
    }
    auto bytePerFrame = static_cast<uint64_t>(outputFormat.mBytesPerFrame);
    outputData_ = std::make_unique<AudioBufferList>();
    outputData_->mNumberBuffers = 1;
    outputData_->mBuffers[0].mNumberChannels = audioConfig->channels;
    outputData_->mBuffers[0].mDataByteSize = static_cast<UInt32>(bytePerFrame * 1024);
    outputData_->mBuffers[0].mData = malloc(outputData_->mBuffers[0].mDataByteSize);
    isOpen_ = true;
    return true;
}

void iOSAACHWDecoder::close() noexcept {
    reset();
    worker_.stop();
}

AVFramePtr iOSAACHWDecoder::getDecodeFrame() noexcept {
    AVFramePtr frame = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pendingFrames_.empty()) {
        frame = std::move(pendingFrames_.front());
        pendingFrames_.pop_front();
    }
    return frame;
}

void iOSAACHWDecoder::wait() noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return !pendingFrames_.empty() || !isOpen_;});
}

}
