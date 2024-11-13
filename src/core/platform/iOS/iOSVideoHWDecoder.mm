//
//  iOSVideoHWDecoder.m
//  slark
//
//  Created by Nevermore on 2024/10/20.
//
#import "iOSVideoHWDecoder.h"
#import "iOSMediaUtil.h"
#include "Log.hpp"
#include "DecoderConfig.h"

namespace slark {

void decompressionOutputCallback(void* decompressionOutputRefCon,
                   void* sourceFrameRefCon,
                   OSStatus status,
                   VTDecodeInfoFlags infoFlags,
                   CVImageBufferRef pixelBuffer,
                   CMTime,
                   CMTime) {
    @autoreleasepool {
        auto framePtr = std::unique_ptr<AVFrame>(reinterpret_cast<AVFrame*>(sourceFrameRefCon));
        bool isSuccess = false;
        do {
            if (status != noErr) {
                break;
            }
            
            if (framePtr->isDiscard) {
                break;
            }
            
            OSType formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
            if (formatType != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
                LogI("formatType error:{}", formatType);
                break;
            }
            if (kVTDecodeInfo_FrameDropped & infoFlags) {
                LogI("droped\n");
                break;
            }
            framePtr->stats.decodedStamp = Time::nowTimeStamp();
            if (framePtr->info.has_value()) {
                auto videoInfo = std::any_cast<VideoFrameInfo>(framePtr->info);
                videoInfo.format = FrameFormat::VideoToolBox;
                framePtr->info = videoInfo;
            }
            framePtr->opaque = CVBufferRetain(pixelBuffer);
            isSuccess = true;
        } while (false);
        if (isSuccess) {
            auto decoder = reinterpret_cast<iOSVideoHWDecoder*>(decompressionOutputRefCon);
            if (decoder->receiveFunc) {
                decoder->receiveFunc(std::move(framePtr));
            }
        }
    }
}

std::string_view getErrorStr(OSStatus status) {
    using namespace std::string_view_literals;
    switch (status) {
        case kVTInvalidSessionErr:
            return "kVTInvalidSessionErr"sv;
        case kVTVideoDecoderBadDataErr:
            return "kVTVideoDecoderBadDataErr"sv;
        case kVTVideoDecoderUnsupportedDataFormatErr:
            return "kVTVideoDecoderUnsupportedDataFormatErr"sv;
        case kVTVideoDecoderMalfunctionErr:
            return "kVTVideoDecoderMalfunctionErr"sv;
        default:
            return "UNKNOWN"sv;
    }
}

iOSVideoHWDecoder::~iOSVideoHWDecoder() {
    reset();
}

void iOSVideoHWDecoder::reset() noexcept {
    isOpen_ = false;
    isFlushed_ = false;
    if (decodeSession_) {
        VTDecompressionSessionWaitForAsynchronousFrames(decodeSession_);
        VTDecompressionSessionInvalidate(decodeSession_);
        CFRetain(decodeSession_);
        decodeSession_ = nullptr;
    }
    if (videoFormatDescription_) {
        CFRelease(videoFormatDescription_);
        videoFormatDescription_ = nullptr;
    }
}

bool iOSVideoHWDecoder::send(AVFramePtr frame) noexcept {
    frame->stats.prepareDecodeStamp = Time::nowTimeStamp();
    auto data = frame->detachData();
    auto sampleBuffer = createSampleBuffer(videoFormatDescription_, static_cast<void*>(data->rawData), static_cast<size_t>(data->length));
    auto framePtr = frame.release();
    uint32_t decoderFlags = 0;
    if (framePtr->isDiscard) {
        decoderFlags |= kVTDecodeFrame_DoNotOutputFrame;
    }
    auto status = VTDecompressionSessionDecodeFrame(decodeSession_, sampleBuffer, decoderFlags, reinterpret_cast<void*>(framePtr), 0);
    if (status != noErr) {
        if (status == kVTInvalidSessionErr) {
        }
        if (status == kVTVideoDecoderMalfunctionErr) {
            
        }
    } else {
        status = VTDecompressionSessionWaitForAsynchronousFrames(decodeSession_);
    }
    if (sampleBuffer) {
        CFRelease(sampleBuffer);
    }
    return status == noErr;
}

void iOSVideoHWDecoder::flush() noexcept {
    if (!isOpen_) {
        return;
    }
    VTDecompressionSessionFinishDelayedFrames(decodeSession_);
    isFlushed_ = true;
}

bool iOSVideoHWDecoder::createDecodeSession() noexcept {
    OSStatus status = noErr;
    auto config = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    if (!config) {
        LogI("config error");
        return false;
    }
    if (config->codec == CodecType::H264) {
        const uint8_t* const parameterSetPointers[2] = {config->sps->rawData, config->pps->rawData};
        const size_t parameterSetSizes[2] = { config->sps->length, config->pps->length };
        status = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,
                                                                     2,
                                                                     parameterSetPointers,
                                                                     parameterSetSizes,
                                                                     static_cast<int>(config->naluHeaderLength + 1),
                                                                     &videoFormatDescription_);
    } else {
        if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)) {
            LogI("not support hevc");
            return false;
        }
        const uint8_t* const parameterSetPointers[3] = {config->sps->rawData, config->pps->rawData, config->vps->rawData};
        const size_t parameterSetSizes[3] = { config->sps->length, config->pps->length };
        status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(kCFAllocatorDefault,
                                                                     3,
                                                                     parameterSetPointers,
                                                                     parameterSetSizes,
                                                                     config->naluHeaderLength,
                                                                     NULL,
                                                                     &videoFormatDescription_);
    }

    if (status != noErr) {
        LogI("create decoder video format description error:{}", status);
        return false;
    }
    CFDictionaryRef attrs = NULL;
    const void *keys[] = { kCVPixelBufferPixelFormatTypeKey };
    uint32_t v = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    const void *values[] = { CFNumberCreate(nullptr, kCFNumberSInt32Type, &v) };
    attrs = CFDictionaryCreate(nullptr, keys, values, 1, nullptr, nullptr);
    
    VTDecompressionOutputCallbackRecord callBackRecord;
    callBackRecord.decompressionOutputCallback = decompressionOutputCallback;
    callBackRecord.decompressionOutputRefCon = this;

    status = VTDecompressionSessionCreate(kCFAllocatorDefault,
                                          videoFormatDescription_,
                                          NULL, attrs,
                                          &callBackRecord,
                                          &decodeSession_);
    if (status != noErr) {
        LogI("create decoder, set call back error:{}", status);
        return false;
    }
    CFRelease(attrs);
    isOpen_ = true;
    return true;
}

bool iOSVideoHWDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    reset();
    config_ = config;
    return createDecodeSession();
}

void iOSVideoHWDecoder::close() noexcept {
    reset();
    config_.reset();
}

}
