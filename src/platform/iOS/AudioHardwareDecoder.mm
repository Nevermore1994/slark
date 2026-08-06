
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AudioHardwareDecoder.h"

#include "AudioDescription.h"
#include "Log.hpp"
#include "ScopeGuard.h"

namespace slark {

static constexpr OSStatus kInvalidStatus = -1;
static constexpr UInt32 kAACFrameSize = 1024;

OSStatus AACDecodeInputDataProc(AudioConverterRef,
                                UInt32* ioNumberDataPackets,
                                AudioBufferList* ioData,
                                AudioStreamPacketDescription** outDataPacketDescription,
                                void* inUserData) {
    if (inUserData == nullptr) {
        return kInvalidStatus;
    }
    auto decoder = reinterpret_cast<AudioHardwareDecoder*>(inUserData);
    auto framePtr = decoder->getDecodingFrame();
    if (!framePtr) {
        LogI("exit aac decode");
        return kInvalidStatus;
    }
    auto dataPtr = framePtr->detachData();
    if (dataPtr == nullptr) {
        LogE("decode audio data is nullptr");
        *ioNumberDataPackets = 0;
        return kInvalidStatus;
    }
    auto audioFrameInfo = framePtr->audioInfo();
    if (!audioFrameInfo) {
        LogE("decode audio frame info is nullptr");
        return kInvalidStatus;
    }
    // Point directly at the input data to avoid a copy; AudioConverter consumes it
    // synchronously before the callback returns
    ioData->mBuffers[0].mDataByteSize = static_cast<UInt32>(dataPtr->length());
    ioData->mBuffers[0].mNumberChannels = audioFrameInfo->channels;
    ioData->mBuffers[0].mData = dataPtr->data();
    decoder->retainInputData(std::move(dataPtr));
    *ioNumberDataPackets = 1;
    if (outDataPacketDescription && *outDataPacketDescription) {
        (*outDataPacketDescription)[0].mStartOffset = 0;
        (*outDataPacketDescription)[0].mVariableFramesInPacket = 0;
        (*outDataPacketDescription)[0].mDataByteSize =
            static_cast<UInt32>(ioData->mBuffers[0].mDataByteSize);
    }
    return noErr;
}

static MPEG4ObjectID getAACProfile(uint8_t profile) {
    switch (profile) {
        case static_cast<uint8_t>(AudioProfile::AAC_MAIN):
            return kMPEG4Object_AAC_Main;
        case static_cast<uint8_t>(AudioProfile::AAC_LC):
            return kMPEG4Object_AAC_LC;
        case static_cast<uint8_t>(AudioProfile::AAC_SSR):
            return kMPEG4Object_AAC_SSR;
        case static_cast<uint8_t>(AudioProfile::AAC_LTP):
            return kMPEG4Object_AAC_LTP;
        case static_cast<uint8_t>(AudioProfile::AAC_HE):
            return kMPEG4Object_AAC_SBR;
        case static_cast<uint8_t>(AudioProfile::AAC_HE_SP):
            // HE-AAC v2 (PS) is distinguished via mFormatID=kAudioFormatMPEG4AAC_HE_V2;
            // mFormatFlags also uses SBR, the iOS SDK has no separate PS enum value
            return kMPEG4Object_AAC_SBR;
        default:
            return kMPEG4Object_AAC_LC;
    }
}

static AudioFormatID getAACFormatID(uint8_t profile) {
    switch (static_cast<AudioProfile>(profile)) {
        case AudioProfile::AAC_HE:
            return kAudioFormatMPEG4AAC_HE;
        case AudioProfile::AAC_HE_SP:
            return kAudioFormatMPEG4AAC_HE_V2;
        default:
            return kAudioFormatMPEG4AAC;
    }
}

AudioHardwareDecoder::AudioHardwareDecoder() : IDecoder(DecoderType::AACHardwareDecoder) {}

AudioHardwareDecoder::~AudioHardwareDecoder() {
    close();
}

void AudioHardwareDecoder::reset() noexcept {
    isOpen_ = false;
    if (decodeSession_) {
        AudioConverterDispose(decodeSession_);
        decodeSession_ = nullptr;
    }
    outputData_.reset();
    outputBuffer_.reset();
    spareBuffer_.reset();
}

void AudioHardwareDecoder::flush() noexcept {
    isCompleted_ = false;
}

Error AudioHardwareDecoder::decode(AVFrameRefPtr& frame) noexcept {
    if (frame->info && frame->info->isEndOfStream) {
        isCompleted_ = true;
        return Error::Success;
    }
    decodingFrame_ = frame;
    ScopeGuard decodingFrameGuard([this] {
        decodingFrame_.reset();
    });
    UInt32 outputDataPacketSize = kAACFrameSize;
    OSStatus status = AudioConverterFillComplexBuffer(
        decodeSession_, AACDecodeInputDataProc, reinterpret_cast<void*>(this),
        &outputDataPacketSize, outputData_.get(), nullptr);
    inputData_.reset();
    if (status != noErr) {
        LogE("aac decode error: {}", status);
        return Error::CodecError;
    }
    if (!decodingFrame_) {
        LogE("decode error, current decode frame is nullptr");
        return Error::CodecError;
    }
    // Double-buffer alternation: move the current buffer to the frame and use the spare
    // next, avoiding a per-frame heap allocation
    auto byteSize = outputData_->mBuffers[0].mDataByteSize;
    outputBuffer_->setSize(byteSize);
    decodingFrame_->data = std::move(outputBuffer_);
    // Switch to the spare buffer
    outputBuffer_ = std::move(spareBuffer_);
    auto bufferCapacity = static_cast<uint64_t>(outputData_->mBuffers[0].mDataByteSize);
    if (!outputBuffer_ || outputBuffer_->capacity() < bufferCapacity) {
        outputBuffer_ = Data::create(bufferCapacity);
    }
    outputData_->mBuffers[0].mData = outputBuffer_->data();
    // Lazily create the spare buffer; once the frame is consumed it becomes the next spare
    if (!spareBuffer_ || spareBuffer_->capacity() < bufferCapacity) {
        spareBuffer_ = Data::create(bufferCapacity);
    }
    invokeReceiveFunc(DecoderData{std::move(decodingFrame_), Error::Success, currentSeekEpoch_});
    return Error::Success;
}

bool AudioHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    reset();
    config_ = config;
    auto audioConfig = std::dynamic_pointer_cast<AudioDecoderConfig>(config);
    if (!audioConfig) {
        LogE("open aac decoder failed: not audio config");
        return false;
    }

    AudioStreamBasicDescription inputFormat{};
    inputFormat.mFormatID = getAACFormatID(audioConfig->profile);
    inputFormat.mSampleRate = static_cast<Float64>(audioConfig->sampleRate);
    inputFormat.mChannelsPerFrame = audioConfig->channels;
    inputFormat.mFormatFlags = static_cast<AudioFormatFlags>(getAACProfile(audioConfig->profile));
    inputFormat.mFramesPerPacket = kAACFrameSize;

    AudioStreamBasicDescription outputFormat{};
    outputFormat.mFormatID = kAudioFormatLinearPCM;
    outputFormat.mSampleRate = static_cast<Float64>(audioConfig->sampleRate);
    outputFormat.mChannelsPerFrame = audioConfig->channels;
    outputFormat.mFramesPerPacket = 1;
    outputFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    outputFormat.mBitsPerChannel =
        audioConfig->bitsPerSample == 0 ? 16 : audioConfig->bitsPerSample;
    outputFormat.mBytesPerFrame = outputFormat.mBitsPerChannel * outputFormat.mChannelsPerFrame / 8;
    outputFormat.mBytesPerPacket = outputFormat.mBytesPerFrame * outputFormat.mFramesPerPacket;

    OSStatus status = AudioConverterNew(&inputFormat, &outputFormat, &decodeSession_);
    if (status != noErr) {
        LogE("create audio converter failed: {}", status);
        return false;
    }
    ScopeGuard sessionGuard([this] {
        AudioConverterDispose(decodeSession_);
        decodeSession_ = nullptr;
    });

    auto bufferSize = static_cast<uint64_t>(outputFormat.mBytesPerFrame) * kAACFrameSize;
    outputBuffer_ = Data::create(bufferSize);
    if (!outputBuffer_ || !outputBuffer_->data()) {
        LogE("allocate output buffer failed");
        return false;
    }

    outputData_ = std::make_unique<AudioBufferList>();
    outputData_->mNumberBuffers = 1;
    outputData_->mBuffers[0].mNumberChannels = audioConfig->channels;
    outputData_->mBuffers[0].mDataByteSize = static_cast<UInt32>(bufferSize);
    outputData_->mBuffers[0].mData = outputBuffer_->data();

    sessionGuard.dismiss();
    isOpen_ = true;
    return true;
}

void AudioHardwareDecoder::close() noexcept {
    reset();
    config_.reset();
}
}  // namespace slark
