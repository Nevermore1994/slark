//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AACHardwareDecoder.h"
#include "AudioRender.h"
#include "MediaDefs.h"
#include "PlatformRegistry.h"
#include "VideoHardwareDecoder.h"
#include "iOSEGLContext.h"

namespace slark {

namespace {

// 静态初始化确保平台注册在任何使用之前完成
struct IOSPlatformRegistrar {
    IOSPlatformRegistrar() noexcept {
        auto& registry = PlatformRegistry::instance();

        // 注册音频渲染工厂
        registry.registerAudioRenderFactory([](const std::shared_ptr<AudioInfo>& info) {
            return std::make_shared<AudioRender>(info);
        });

        // 注册GL上下文工厂
        registry.registerGLContextFactory([]() -> IEGLContextRefPtr {
            return std::make_shared<iOSEGLContext>();
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

// 静态实例，程序启动时自动执行注册
static IOSPlatformRegistrar iosPlatformRegistrar;

} // namespace

} // namespace slark
