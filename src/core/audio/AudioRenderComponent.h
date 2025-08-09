//
// Created by Nevermore on 2023/8/11.
// slark AudioRenderComponent
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once

#include <functional>
#include <shared_mutex>
#include "AudioInfo.h"
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

constexpr double kDefaultAudioBufferCacheTime = 0.2; // 200ms,

using PullDataFunc = std::function<AVFrameRefPtr(uint32_t)>;

class AudioRenderComponent: public slark::NonCopyable,
        public InputNode,
        public std::enable_shared_from_this<AudioRenderComponent> {
public:
    explicit AudioRenderComponent(std::shared_ptr<AudioInfo> info);

    ~AudioRenderComponent() override;

    bool send(AVFrameRefPtr frame) noexcept override;

    bool process(AVFrameRefPtr frame) noexcept override;

    [[nodiscard]] std::shared_ptr<AudioInfo> audioInfo() const noexcept;

    void reset() noexcept;
    
    void start() noexcept;

    void pause() noexcept;

    void stop() noexcept;

    void setVolume(float volume) noexcept;

    void flush() noexcept;

    void seek(double time) noexcept;

    Time::TimePoint playedTime() noexcept;
    
    bool isFull() noexcept {
        if (!audioBuffer_) {
            LogE("audio buffer is nullptr");
            return true;
        }
        return isFull_ || audioBuffer_->isFull();
    }
            
    bool isHungry() noexcept {
        return isHungry_;
    }

    RenderStatus status() const {
        if (auto pimpl = pimpl_.load()) {
            return pimpl->status();
        }
        return RenderStatus::Unknown;
    }

    void renderEnd() noexcept;

    void setPullDataFunc(
        PullDataFunc&& pullAudioDataFunc
    ) noexcept {
        pullDataFunc_.reset(std::make_shared<PullDataFunc>(std::move(pullAudioDataFunc)));
    }
private:
    void init() noexcept;
public:
    std::function<void(Time::TimePoint)> firstFrameRenderCallBack;
private:
    bool isFirstFrameRendered = false;
    bool isFull_ = false;
    std::atomic<bool> isHungry_ = false;
    std::shared_ptr<AudioInfo> audioInfo_;
    std::unique_ptr<SyncRingBuffer<uint8_t>> audioBuffer_;
    AtomicSharedPtr<IAudioRender> pimpl_;
    AtomicSharedPtr<PullDataFunc> pullDataFunc_;
};

}
