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
#include "Clock.h"

#ifdef SLARK_IOS
#include "AudioRender.h"
#else

#endif

namespace slark {

constexpr uint64_t kDefaultAudioBufferSize = 16 * 1024;
class AudioRenderComponent: public slark::NonCopyable, public InputNode {
public:
    explicit AudioRenderComponent(std::shared_ptr<AudioInfo> info);
    ~AudioRenderComponent() override = default;

    void send(AVFrameRefPtr frame) noexcept override;
    void process(AVFrameRefPtr frame) noexcept override;
    [[nodiscard]] std::shared_ptr<AudioInfo> audioInfo() const noexcept;
    void clear() noexcept;
    void reset() noexcept;
    
    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;
    void setVolume(float volume) noexcept;
    void flush() noexcept;
    void seek(Time::TimePoint time) noexcept;

    Time::TimePoint playedTime() {
        return clock_.time();
    }
    
    bool isFull() noexcept {
        bool isFull = false;
        frames_.withReadLock([&](auto&){
            isFull = audioBuffer_.isFull();
        });
        return isFull;
    }
    
    RenderStatus status() const {
        if (pimpl_) {
            return pimpl_->status();
        }
        return RenderStatus::Unknown;
    }
    
    Clock& clock() noexcept {
        return clock_;
    }
    
private:
    void init() noexcept;
public:
    std::function<uint32_t(uint8_t*, uint32_t)> pullAudioData;
    std::function<void(Time::TimePoint)> firstFrameRenderCallBack;
private:
    bool isFirstFrameRendered = false;
    uint64_t renderedDataLength_ = 0;
    Clock clock_;
    std::mutex renderMutex_;
    std::shared_ptr<AudioInfo> audioInfo_;
    RingBuffer<uint8_t, kDefaultAudioBufferSize> audioBuffer_;
    Synchronized<std::deque<AVFrameRefPtr>, std::shared_mutex> frames_;
    std::unique_ptr<IAudioRender> pimpl_;
};

}
