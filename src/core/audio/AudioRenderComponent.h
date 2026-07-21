//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "AudioInfo.h"
#include "Clock.h"
#include "IPipelineComponent.h"
#include "Node.h"
#include "NonCopyable.h"
#include "SPSCRingBuffer.hpp"
#include "Synchronized.hpp"
#include "Time.hpp"

#include <functional>

namespace slark {

class AudioRenderComponent : public IPipelineComponent,
                             public slark::NonCopyMovable,
                             public InputNode,
                             public std::enable_shared_from_this<AudioRenderComponent> {
public:
    explicit AudioRenderComponent(std::shared_ptr<AudioInfo> info);

    ~AudioRenderComponent();

    bool send(AVFrameRefPtr frame) noexcept override;

    bool process(AVFrameRefPtr frame) noexcept override;

    [[nodiscard]] std::shared_ptr<AudioInfo> audioInfo() const noexcept;

    void reset() noexcept;

    void start() noexcept override;

    void pause() noexcept override;

    void stop() noexcept;

    void close() noexcept override {
        stop();
    }

    void flush() noexcept override;

    [[nodiscard]] bool isRunning() const noexcept override;

    void setVolume(float volume) noexcept;

    void seek(double time) noexcept;

    Time::TimePoint playedTime() noexcept;

    void setRenderEndCallback(std::function<void()> callback) noexcept {
        renderEndCallback_ = std::move(callback);
    }

    void setAudioTimeUpdater(std::function<void(Time::TimePoint)> updater) noexcept {
        audioTimeUpdater_.reset(
            std::make_shared<std::function<void(Time::TimePoint)>>(std::move(updater)));
    }

    [[nodiscard]] uint32_t available() const noexcept {
        return audioBuffer_ ? audioBuffer_->available() : 0;
    }

    [[nodiscard]] uint32_t readable() const noexcept {
        return audioBuffer_ ? audioBuffer_->length() : 0;
    }

    RenderStatus status() const {
        if (auto pimpl = pimpl_.load()) {
            return pimpl->status();
        }
        return RenderStatus::Unknown;
    }

    void renderEnd() noexcept;

    void setDecoderCompleted() noexcept {
        decoderCompleted_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool isDecoderCompleted() const noexcept {
        return decoderCompleted_.load(std::memory_order_acquire);
    }

private:
    void init() noexcept;

    static constexpr uint32_t kAudioBufferSize = 262144;

private:
    std::shared_ptr<AudioInfo> audioInfo_;
    std::unique_ptr<SPSCRingBuffer<uint8_t, kAudioBufferSize>> audioBuffer_;

    ThreadSafeClock clock_;
    std::atomic<uint64_t> renderedDataLength_{0};
    std::atomic<uint64_t> maxPlayedTimeUs_{0};
    Time::TimePoint offsetTime_{};

    std::atomic<bool> decoderCompleted_{false};

    AtomicSharedPtr<IAudioRender> pimpl_;
    AtomicSharedPtr<std::function<void(Time::TimePoint)>> audioTimeUpdater_;
    std::function<void()> renderEndCallback_;
};

} // namespace slark
