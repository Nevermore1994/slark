//
// Created by Nevermore on 2023/8/11.
// slark AudioRenderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include <memory>
#include <functional>
#include "AudioDefine.h"
#include "NonCopyable.h"
#include "AVFrame.hpp"
#include "Time.hpp"
#include "Node.h"

#ifdef SLARK_IOS
#include "AudioRender.h"
#else

#endif

namespace slark::Audio {

class AudioRenderComponent: public slark::NonCopyable, public InputNode {
public:
    explicit AudioRenderComponent(std::shared_ptr<AudioInfo> info);
    ~AudioRenderComponent() override = default;

    void receive(AVFrameRefPtr frame) noexcept override;
    void process(AVFrameRefPtr frame) noexcept override;
    const std::shared_ptr<AudioInfo> audioInfo() const noexcept;
    void clear() noexcept;
    void reset() noexcept;
    
    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;
    void setVolume(float volume) noexcept;
    void flush() noexcept;
public:
    std::function<void(AVFrameRefPtr)> completion;
private:
    std::shared_ptr<AudioInfo> audioInfo_;
    AVFrameRefPtrSafeDeque frames_;
    std::unique_ptr<IAudioRender> pimpl_;

};

}
