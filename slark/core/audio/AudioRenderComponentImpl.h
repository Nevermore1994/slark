//
// Created by Nevermore on 2023/8/11.
// slark AudioRenderComponentImpl
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include "AudioComponent.h"
#include "AudioInfo.h"
#ifdef SLARK_IOS

#else

#endif

namespace slark::Audio {

class AudioRenderComponent::AudioRenderComponentImpl {
public:
    AudioRenderComponentImpl(std::unique_ptr<slark::AudioInfo> info);
    ~AudioRenderComponentImpl();

    void receive(AVFrameRefPtr frame) noexcept;
    [[nodiscard]] inline const AudioInfo& AudioInfo() const noexcept {
        SAssert(info_ != nullptr, "error, audio info is nullptr");
        return *info_;
    }
private:
    std::unique_ptr<slark::AudioInfo> info_;
};

}
