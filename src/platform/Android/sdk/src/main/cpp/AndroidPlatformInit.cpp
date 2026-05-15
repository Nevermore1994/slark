//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AACHardwareDecoder.h"
#include "AAudioRender.h"
#include "AndroidEGLContext.h"
#include "MediaDefs.h"
#include "PlatformRegistry.h"
#include "VideoHardwareDecoder.h"

namespace slark {

namespace {

struct AndroidPlatformRegistrar {
    AndroidPlatformRegistrar() noexcept {
        auto& registry = PlatformRegistry::instance();

        // 注册音频渲染工厂（AAudio callback 模式，低延迟）
        registry.registerAudioRenderFactory([](const std::shared_ptr<AudioInfo>& info) {
            return std::make_shared<AAudioRender>(info);
        });

        // 注册GL上下文工厂
        registry.registerGLContextFactory([]() -> IEGLContextRefPtr {
            return std::make_shared<AndroidEGLContext>();
        });

        // 注册AAC硬件解码器
        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_AUDIO_AAC) + "HW",
                                   DecoderType::AACHardwareDecoder);
        registry.registerDecoderInfo(DecoderType::AACHardwareDecoder, AACHardwareDecoder::info());

        // 注册视频硬件解码器
        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_VIDEO_AVC) + "HW",
                                   DecoderType::VideoHardwareDecoder);
        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_VIDEO_HEVC) + "HW",
                                   DecoderType::VideoHardwareDecoder);
        registry.registerDecoderInfo(DecoderType::VideoHardwareDecoder,
                                     VideoHardwareDecoder::info());
    }
};

static AndroidPlatformRegistrar androidPlatformRegistrar;

} // namespace

} // namespace slark
