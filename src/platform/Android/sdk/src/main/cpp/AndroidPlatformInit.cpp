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

        registry.registerAudioRenderFactory([](const std::shared_ptr<AudioInfo>& info) {
            return std::make_shared<AAudioRender>(info);
        });

        registry.registerGLContextFactory([]() -> IEGLContextRefPtr {
            return std::make_shared<AndroidEGLContext>();
        });

        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_AUDIO_AAC) + "HW",
                                   DecoderType::AACHardwareDecoder);
        registry.registerDecoderInfo(DecoderType::AACHardwareDecoder, AACHardwareDecoder::info());

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

std::shared_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& audioInfo) {
    return PlatformRegistry::instance().createAudioRender(audioInfo);
}

} // namespace slark
