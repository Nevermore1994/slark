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

#if defined(SLARK_IOS) || defined(SLARK_ANDROID)
#include "AudioRender.h"
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
    void reset() noexcept;
    
    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;
    void setVolume(float volume) noexcept;
    void flush() noexcept;
    void seek(long double time) noexcept;

    Time::TimePoint playedTime() {
        Time::TimePoint latency;
        if (auto pimpl = pimpl_.load()) {
            latency = pimpl->latency();
        }
        return clock_.time() - latency;
    }
    
    bool require(uint32_t size) noexcept {
        return audioBuffer_.require(size);
    }
    
    RenderStatus status() const {
        if (auto pimpl = pimpl_.load()) {
            return pimpl->status();
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
    std::atomic<uint64_t> renderedDataLength_ = 0;
    Clock clock_;
    std::shared_ptr<AudioInfo> audioInfo_;
    SPSCRingBuffer<uint8_t, kDefaultAudioBufferSize> audioBuffer_;
    AtomicSharedPtr<IAudioRender> pimpl_;
};

}
