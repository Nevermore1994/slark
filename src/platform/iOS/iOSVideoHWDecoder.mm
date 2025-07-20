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

struct DecodeInfo {
    int64_t pts = 0;
    uint64_t timeScale = 1;
    bool isDiscard = false;
    
    double ptsTime() {
        return static_cast<double>(pts) / static_cast<double>(timeScale);
    }
};

void decompressionOutputCallback(void* decompressionOutputRefCon,
                   void* sourceFrameRefCon,
                   OSStatus status,
                   VTDecodeInfoFlags infoFlags,
                   CVImageBufferRef pixelBuffer,
                   CMTime,
                   CMTime) {
    @autoreleasepool {
        auto* decodeInfo = reinterpret_cast<DecodeInfo*>(sourceFrameRefCon);;
        if (decodeInfo->isDiscard) {
            LogE("video decode discard frame:{}", decodeInfo->ptsTime());
            return;
        }
        auto decoder = reinterpret_cast<iOSVideoHWDecoder*>(decompressionOutputRefCon);
        if (!decoder) {
            LogE("decoder is nullptr");
            return;
        }
        CVPixelBufferRef opaque = nullptr;
        do {
            if (status != noErr) {
                LogE("OSStatus:{}", status);
                break;
            }
            
            if (decodeInfo->isDiscard) {
                LogE("discard video frame:{}", decodeInfo->ptsTime());
                break;
            }
            
            OSType formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
            if (formatType != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
                LogI("formatType error:{}", formatType);
                break;
            }
            if (kVTDecodeInfo_FrameDropped & infoFlags) {
                LogI("droped");
                break;
            }
            opaque = CVPixelBufferRetain(pixelBuffer);
        } while (false);
        if (opaque == nullptr) {
            return;
        }
        
        auto frame = std::make_shared<AVFrame>(AVFrameType::Video);
        frame->opaque = opaque;
        frame->pts = decodeInfo->pts;
        frame->timeScale = decodeInfo->timeScale;
        auto videoInfo = std::make_shared<VideoFrameInfo>();
        videoInfo->format = FrameFormat::VideoToolBox;
        decoder->invokeReceiveFunc(std::move(frame));
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
    @autoreleasepool {
        if (decodeSession_) {
            VTDecompressionSessionWaitForAsynchronousFrames(decodeSession_);
            VTDecompressionSessionInvalidate(decodeSession_);
            CFRelease(decodeSession_);
            decodeSession_ = nullptr;
        }
        if (videoFormatDescription_) {
            CFRelease(videoFormatDescription_);
            videoFormatDescription_ = nullptr;
        }
    }

}

DecoderErrorCode iOSVideoHWDecoder::decode(AVFrameRefPtr& frame) noexcept {
    auto& data = frame->data;
    auto videoInfo = std::dynamic_pointer_cast<VideoFrameInfo>(frame->info);
    if (videoInfo->isEndOfStream) {
        flush(); //Output remain frames
        return DecoderErrorCode::Success;
    }
    auto sampleBuffer = createSampleBuffer(videoFormatDescription_, static_cast<void*>(data->rawData), static_cast<size_t>(data->length));
    auto decodeInfo = new (std::nothrow) DecodeInfo();
    if (decodeInfo == nullptr) {
        return DecoderErrorCode::Unknown;
    }
    decodeInfo->pts = frame->pts;
    decodeInfo->timeScale = frame->timeScale;
    decodeInfo->isDiscard = frame->isDiscard;
    
    auto status = VTDecompressionSessionDecodeFrame(decodeSession_, sampleBuffer, kVTDecodeFrame_EnableAsynchronousDecompression, reinterpret_cast<void*>(decodeInfo), nullptr);
    if (sampleBuffer) {
        CFRelease(sampleBuffer);
        sampleBuffer = nullptr;
    }
    if (status == noErr) {
        return DecoderErrorCode::Success;
    }
    return DecoderErrorCode::Unknown;
}

void iOSVideoHWDecoder::flush() noexcept {
    if (!isOpen_) {
        return;
    }
    VTDecompressionSessionFinishDelayedFrames(decodeSession_);
    VTDecompressionSessionWaitForAsynchronousFrames(decodeSession_);
}

bool iOSVideoHWDecoder::createDecodeSession() noexcept {
    OSStatus status = noErr;
    auto config = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    if (!config) {
        LogI("config error");
        return false;
    }
    if (config->mediaInfo == MEDIA_MIMETYPE_VIDEO_AVC) {
        const uint8_t* const parameterSetPointers[2] = {config->sps->rawData, config->pps->rawData};
        const size_t parameterSetSizes[2] = { config->sps->length, config->pps->length };
        status = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,
                                                                     2,
                                                                     parameterSetPointers,
                                                                     parameterSetSizes,
                                                                     static_cast<int>(config->naluHeaderLength),
                                                                     &videoFormatDescription_);
    } else if (config->mediaInfo == MEDIA_MIMETYPE_VIDEO_HEVC) {
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
    } else {
        LogE("not support media type:{}", config->mediaInfo);
        return false;
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
    attrs = NULL;
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
