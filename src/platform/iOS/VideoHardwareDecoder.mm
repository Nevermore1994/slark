
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

// slark
#import "VideoHardwareDecoder.h"

#include "DecoderConfig.h"
#include "Log.hpp"
#include "ScopeGuard.h"
#import "iOSMediaUtil.h"

namespace slark {

struct DecodeInfo {
    int64_t pts = 0;
    uint64_t timeScale = 1;
    bool isDiscard = false;

    double ptsTime() const {
        return timeScale > 0 ? static_cast<double>(pts) / static_cast<double>(timeScale) : 0.0;
    }
};

static void decompressionOutputCallback(void* decompressionOutputRefCon,
                                        void* sourceFrameRefCon,
                                        OSStatus status,
                                        VTDecodeInfoFlags infoFlags,
                                        CVImageBufferRef pixelBuffer,
                                        CMTime,
                                        CMTime) {
    // VideoToolbox 可能在回调线程内部产生 autorelease 对象，保留 autoreleasepool 防止泄漏
    @autoreleasepool {
        auto* decodeInfo = reinterpret_cast<DecodeInfo*>(sourceFrameRefCon);
        auto decoder = reinterpret_cast<VideoHardwareDecoder*>(decompressionOutputRefCon);
        if (!decoder) {
            LogE("decoder is nullptr");
            delete decodeInfo;
            return;
        }
        const bool isDiscardFrame = decodeInfo->isDiscard;
        CVPixelBufferRef opaque = nullptr;
        do {
            if (isDiscardFrame) {
                // isDiscard 帧仍需回调给 pipeline，用于 seek 预览和 seek 完成判断
                break;
            }
            if (status != noErr) {
                LogE("decode OSStatus:{}", status);
                break;
            }
            if (kVTDecodeInfo_FrameDropped & infoFlags) {
                LogI("frame dropped");
                break;
            }
            OSType formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
            if (formatType != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange &&
                formatType != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
                LogE("unexpected pixel format:{}", formatType);
                break;
            }
            opaque = CVPixelBufferRetain(pixelBuffer);
        } while (false);

        auto pts = decodeInfo->pts;
        auto timeScale = decodeInfo->timeScale;
        delete decodeInfo;

        if (opaque == nullptr && !isDiscardFrame) {
            return;
        }
        auto frame = std::shared_ptr<AVFrame>(new AVFrame(AVFrameType::Video), [](AVFrame* f) {
            auto vi = f->videoInfo();
            if (vi && vi->format == FrameFormat::VideoToolBox && f->opaque) {
                auto* pb = reinterpret_cast<CVPixelBufferRef>(f->opaque);
                f->opaque = nullptr;
                CVPixelBufferRelease(pb);
            }
            delete f;
        });
        frame->opaque = opaque;
        frame->pts = pts;
        frame->timeScale = timeScale;
        frame->isDiscard = isDiscardFrame;
        auto videoInfo = std::make_shared<VideoFrameInfo>();
        videoInfo->format = FrameFormat::VideoToolBox;
        frame->info = videoInfo;
        decoder->invokeReceiveFunc(DecoderData{std::move(frame)});
    }
}

static std::string_view getVTErrorStr(OSStatus status) {
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

VideoHardwareDecoder::~VideoHardwareDecoder() {
    reset();
}

void VideoHardwareDecoder::reset() noexcept {
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
    // 先等待并 invalidate 完成后，再设置状态标志
    isOpen_ = false;
    isCompleted_ = false;
}

void VideoHardwareDecoder::flush() noexcept {
    if (!isOpen_) {
        return;
    }
    VTDecompressionSessionFinishDelayedFrames(decodeSession_);
    VTDecompressionSessionWaitForAsynchronousFrames(decodeSession_);
    isCompleted_ = false;
}

Error VideoHardwareDecoder::decode(AVFrameRefPtr& frame) noexcept {
    if (frame->info && frame->info->isEndOfStream) {
        flush();
        isCompleted_ = true;
        return Error::Success;
    }
    auto& data = frame->data;
    auto sampleBuffer = createSampleBuffer(videoFormatDescription_,
                                           static_cast<void*>(data->data()),
                                           static_cast<size_t>(data->size()));
    if (!sampleBuffer) {
        LogE("create sample buffer failed");
        return Error::CodecInputError;
    }
    ScopeGuard sampleBufferGuard([sampleBuffer] {
        CFRelease(sampleBuffer);
    });
    auto* decodeInfo =
        new (std::nothrow) DecodeInfo{frame->pts, frame->timeScale, frame->isDiscard};
    if (decodeInfo == nullptr) {
        return Error::CodecError;
    }
    auto status = VTDecompressionSessionDecodeFrame(decodeSession_,
                                                    sampleBuffer,
                                                    kVTDecodeFrame_EnableAsynchronousDecompression,
                                                    reinterpret_cast<void*>(decodeInfo),
                                                    nullptr);
    // Apple 文档：decode 返回错误时 callback 不保证被调用，需主动释放 decodeInfo
    if (status != kVTInvalidSessionErr && status != kVTVideoDecoderMalfunctionErr) {
        if (status != noErr) {
            delete decodeInfo;
        }
        return status == noErr ? Error::Success : Error::CodecError;
    }

    // session 异常，释放旧 decodeInfo 并尝试重建 session 后重试
    delete decodeInfo;
    LogE("decode session error:{}, recreating", getVTErrorStr(status));
    if (!createDecodeSession()) {
        return Error::CodecError;
    }
    auto* retryInfo = new (std::nothrow) DecodeInfo{frame->pts, frame->timeScale, frame->isDiscard};
    if (!retryInfo) {
        return Error::CodecError;
    }
    status = VTDecompressionSessionDecodeFrame(decodeSession_,
                                               sampleBuffer,
                                               kVTDecodeFrame_EnableAsynchronousDecompression,
                                               reinterpret_cast<void*>(retryInfo),
                                               nullptr);
    if (status != noErr) {
        delete retryInfo;
    }
    // sampleBuffer 由 ScopeGuard 统一释放，无需手动 CFRelease
    return status == noErr ? Error::Success : Error::CodecError;
}

// 注意：createDecodeSession 不能在 VTDecompressionSession 异步回调线程上调用，否则 reset() 中的
// VTDecompressionSessionWaitForAsynchronousFrames 会死锁（同步模式下回调和 decode 在同一线程）
bool VideoHardwareDecoder::createDecodeSession() noexcept {
    reset();
    auto config = std::dynamic_pointer_cast<VideoDecoderConfig>(config_);
    if (!config) {
        LogE("video decoder config is null or wrong type");
        return false;
    }
    OSStatus status = noErr;
    if (config->mediaInfo == MEDIA_MIMETYPE_VIDEO_AVC) {
        // naluHeaderLength 合法值为 1/2/4，VideoToolbox 不接受 3
        if (config->naluHeaderLength != 1 && config->naluHeaderLength != 2 &&
            config->naluHeaderLength != 4) {
            LogE("invalid naluHeaderLength:{}", config->naluHeaderLength);
            return false;
        }
        const uint8_t* const paramPtrs[2] = {config->sps->data(), config->pps->data()};
        const size_t paramSizes[2] = {static_cast<size_t>(config->sps->length()),
                                      static_cast<size_t>(config->pps->length())};
        status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
            kCFAllocatorDefault,
            2,
            paramPtrs,
            paramSizes,
            static_cast<int32_t>(config->naluHeaderLength),
            &videoFormatDescription_);
    } else if (config->mediaInfo == MEDIA_MIMETYPE_VIDEO_HEVC) {
        if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)) {
            LogE("HEVC hardware decode not supported");
            return false;
        }
        const uint8_t* const paramPtrs[3] = {config->vps->data(),
                                             config->sps->data(),
                                             config->pps->data()};
        const size_t paramSizes[3] = {static_cast<size_t>(config->vps->length()),
                                      static_cast<size_t>(config->sps->length()),
                                      static_cast<size_t>(config->pps->length())};
        status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(kCFAllocatorDefault,
                                                                     3,
                                                                     paramPtrs,
                                                                     paramSizes,
                                                                     config->naluHeaderLength,
                                                                     nullptr,
                                                                     &videoFormatDescription_);
    } else {
        LogE("unsupported media type:{}", config->mediaInfo);
        return false;
    }
    if (status != noErr) {
        LogE("create format description failed:{}", status);
        return false;
    }

    uint32_t pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    CFNumberRef pixelFormatNum = CFNumberCreate(nullptr, kCFNumberSInt32Type, &pixelFormat);
    const void* keys[] = {kCVPixelBufferPixelFormatTypeKey};
    const void* values[] = {pixelFormatNum};
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, keys, values, 1, nullptr, nullptr);
    CFRelease(pixelFormatNum);
    ScopeGuard attrsGuard([attrs] {
        if (attrs) {
            CFRelease(attrs);
        }
    });

    VTDecompressionOutputCallbackRecord callbackRecord{decompressionOutputCallback, this};
    status = VTDecompressionSessionCreate(kCFAllocatorDefault,
                                          videoFormatDescription_,
                                          nullptr,
                                          attrs,
                                          &callbackRecord,
                                          &decodeSession_);
    if (status != noErr) {
        LogE("create decompression session failed:{}", status);
        return false;
    }
    isOpen_ = true;
    return true;
}

bool VideoHardwareDecoder::open(std::shared_ptr<DecoderConfig> config) noexcept {
    config_ = config;
    return createDecodeSession();
}

void VideoHardwareDecoder::close() noexcept {
    reset();
    config_.reset();
}

} // namespace slark
