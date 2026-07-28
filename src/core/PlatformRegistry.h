//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include "AudioInfo.h"
#include "IDecoder.h"
#include "IEGLContext.h"

namespace slark {

// Platform capability registry — decouples core from platform implementations.
// Platform adapter layers (slark_ios / slark_android) call register*() at init time.
class PlatformRegistry {
 public:
    using AudioRenderFactory =
        std::function<std::shared_ptr<IAudioRender>(const std::shared_ptr<AudioInfo>&)>;
    using GLContextFactory = std::function<IEGLContextRefPtr()>;

    static PlatformRegistry& instance() noexcept {
        static PlatformRegistry reg;
        return reg;
    }

    // Audio render
    void registerAudioRenderFactory(AudioRenderFactory factory) noexcept {
        audioRenderFactory_ = std::move(factory);
    }

    std::shared_ptr<IAudioRender> createAudioRender(
        const std::shared_ptr<AudioInfo>& info) const noexcept {
        if (audioRenderFactory_) {
            return audioRenderFactory_(info);
        }
        return nullptr;
    }

    // GL context
    void registerGLContextFactory(GLContextFactory factory) noexcept {
        glContextFactory_ = std::move(factory);
    }

    IEGLContextRefPtr createGLContext() const noexcept {
        if (glContextFactory_) {
            return glContextFactory_();
        }
        return nullptr;
    }

    // Decoder info
    void registerDecoderInfo(DecoderType type, DecoderTypeInfo info) noexcept {
        decoderInfos_[type] = std::move(info);
    }

    [[nodiscard]] bool hasDecoderInfo(DecoderType type) const noexcept {
        return decoderInfos_.contains(type);
    }

    [[nodiscard]] const DecoderTypeInfo* decoderInfo(DecoderType type) const noexcept {
        auto it = decoderInfos_.find(type);
        return it != decoderInfos_.end() ? &it->second : nullptr;
    }

    [[nodiscard]] const DecoderTypeInfo* getDecoderInfo(DecoderType type) const noexcept {
        return decoderInfo(type);
    }

    [[nodiscard]] bool containsDecoder(DecoderType type) const noexcept {
        return decoderInfos_.contains(type);
    }

    [[nodiscard]] const std::unordered_map<DecoderType, DecoderTypeInfo>& allDecoderInfos()
        const noexcept {
        return decoderInfos_;
    }

    // Media info — mime type to DecoderType mapping
    void registerMediaInfo(std::string mimeType, DecoderType type) noexcept {
        mediaInfo_[std::move(mimeType)] = type;
    }

    [[nodiscard]] const std::unordered_map<std::string, DecoderType>& allMediaInfos()
        const noexcept {
        return mediaInfo_;
    }

    [[nodiscard]] DecoderType getMediaType(const std::string& mimeType) const noexcept {
        auto it = mediaInfo_.find(mimeType);
        return it != mediaInfo_.end() ? it->second : DecoderType::Unknown;
    }

 private:
    PlatformRegistry() = default;
    ~PlatformRegistry() = default;

    AudioRenderFactory audioRenderFactory_;
    GLContextFactory glContextFactory_;
    std::unordered_map<DecoderType, DecoderTypeInfo> decoderInfos_;
    std::unordered_map<std::string, DecoderType> mediaInfo_;
};

}  // namespace slark
