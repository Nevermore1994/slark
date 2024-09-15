//
// Created by Nevermore on 2023/8/11.
// slark AudioRenderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once

#include <functional>
#include <shared_mutex>
#include "AudioDefine.h"
#include "NonCopyable.h"
#include "Node.h"
#include "Synchronized.hpp"
#include "RingBuffer.hpp"
#include "Time.hpp"

#ifdef SLARK_IOS
#include "AudioRender.h"
#else

#endif

namespace slark::Audio {

constexpr uint64_t kDefaultAudioBufferSize = 4 * 1024;
class AudioRenderComponent: public slark::NonCopyable, public InputNode {
public:
    explicit AudioRenderComponent(std::shared_ptr<AudioInfo> info);
    ~AudioRenderComponent() override = default;

    void receive(AVFrameRefPtr frame) noexcept override;
    void process(AVFrameRefPtr frame) noexcept override;
    [[nodiscard]] std::shared_ptr<AudioInfo> audioInfo() const noexcept;
    void clear() noexcept;
    void reset() noexcept;
    
    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;
    void setVolume(float volume) noexcept;
    void flush() noexcept;

    bool isFull() noexcept {
        bool isFull = false;
        frames_.withReadLock([&](auto&){
            isFull = audioBuffer_.isFull();
        });
        return isFull;
    }
private:
    void init() noexcept;
public:
    std::function<void(uint64_t)> renderCompletion;
    std::function<DataPtr(uint64_t)> pullAudioData;
private:
    Time::TimePoint renderPoint_ = 0;
    std::shared_ptr<AudioInfo> audioInfo_;
    RingBuffer<uint8_t, kDefaultAudioBufferSize> audioBuffer_;
    Synchronized<std::deque<AVFrameRefPtr>, std::shared_mutex> frames_;
    std::unique_ptr<IAudioRender> pimpl_;
};

}
