//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AudioHardwareDecoder.h"
#include "AudioRender.h"
#include "MediaDefs.h"
#include "PlatformRegistry.h"
#include "VideoHardwareDecoder.h"
#include "iOSEGLContext.h"

namespace slark {

namespace {

// Static init ensures platform registration happens before any use
struct IOSPlatformRegistrar {
    IOSPlatformRegistrar() noexcept {
        auto& registry = PlatformRegistry::instance();

        registry.registerAudioRenderFactory([](const std::shared_ptr<AudioInfo>& info) {
            return std::make_shared<AudioRender>(info);
        });

        registry.registerGLContextFactory([]() -> IEGLContextRefPtr {
            return std::make_shared<iOSEGLContext>();
        });

        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_AUDIO_AAC) + "HW",
                                   DecoderType::AACHardwareDecoder);
        registry.registerDecoderInfo(DecoderType::AACHardwareDecoder, AudioHardwareDecoder::info());

        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_VIDEO_AVC) + "HW",
                                   DecoderType::VideoHardwareDecoder);
        registry.registerMediaInfo(std::string(MEDIA_MIMETYPE_VIDEO_HEVC) + "HW",
                                   DecoderType::VideoHardwareDecoder);
        registry.registerDecoderInfo(DecoderType::VideoHardwareDecoder,
                                     VideoHardwareDecoder::info());
    }
};

// Static instance, registered automatically at program startup
static IOSPlatformRegistrar iosPlatformRegistrar;

}  // namespace

}  // namespace slark
