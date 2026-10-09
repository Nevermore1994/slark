//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include "AccessGate.hpp"
#include "AudioInfo.h"
#include "Clock.h"
#include "IPipelineComponent.h"
#include "Node.h"
#include "NonCopyable.h"
#include "SPSCRingBuffer.hpp"
#include "Synchronized.hpp"
#include "Time.hpp"

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

    void close() noexcept override;

    void flush() noexcept override;

    [[nodiscard]] bool isRunning() const noexcept override;

    void setVolume(float volume) noexcept;

    void seek(double time) noexcept;

    Time::TimePoint playedTime() noexcept;

    void updateAudioTime() noexcept;

    void setRenderEndCallback(std::function<void()> callback) noexcept {
        renderEndCallback_ = std::move(callback);
    }

    void setAudioTimeUpdater(std::function<void(Time::TimePoint)> updater) noexcept {
        audioTimeUpdater_.reset(
            std::make_shared<std::function<void(Time::TimePoint)>>(std::move(updater)));
    }

    [[nodiscard]] uint32_t available() const noexcept {
        return dataPlane_ ? dataPlane_->buffer.available() : 0;
    }

    [[nodiscard]] uint32_t readable() const noexcept {
        return dataPlane_ ? dataPlane_->buffer.length() : 0;
    }

    RenderStatus status() const {
        if (auto pimpl = pimpl_.load()) {
            return pimpl->status();
        }
        return RenderStatus::Unknown;
    }

    void renderEnd() noexcept;

    void setDecoderCompleted() noexcept {
        if (dataPlane_) {
            dataPlane_->decoderCompleted.store(true, std::memory_order_release);
        }
    }

    [[nodiscard]] bool isDecoderCompleted() const noexcept {
        return dataPlane_ && dataPlane_->decoderCompleted.load(std::memory_order_acquire);
    }

 private:
    static constexpr uint32_t kAudioBufferSize = 262144;

    struct AudioDataPlane {
        void closeAndWait() noexcept {
            gate.closeAndWait();
        }

        void open() noexcept {
            gate.open();
        }

        SPSCRingBuffer<uint8_t, kAudioBufferSize> buffer;
        AccessGate gate;
        std::atomic<uint64_t> renderedDataLength{0};
        std::atomic<uint64_t> offsetTimeUs{0};
        std::atomic<bool> decoderCompleted{false};
    };

    void init() noexcept;
    void resetDataPlane(std::optional<uint64_t> offsetTimeUs,
                        bool resetDecoderCompleted,
                        bool reopen) noexcept;

 private:
    std::shared_ptr<AudioInfo> audioInfo_;
    std::shared_ptr<AudioDataPlane> dataPlane_;

    ThreadSafeClock clock_;
    std::atomic<uint64_t> maxPlayedTimeUs_{0};
    Time::TimePoint offsetTime_{};

    AtomicSharedPtr<IAudioRender> pimpl_;
    AtomicSharedPtr<std::function<void(Time::TimePoint)>> audioTimeUpdater_;
    std::function<void()> renderEndCallback_;
};

}  // namespace slark
