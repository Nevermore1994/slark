//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

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
        static constexpr uint32_t kClosed = 1U << 31;
        static constexpr uint32_t kCountMask = ~kClosed;

        class AccessScope {
         public:
            explicit AccessScope(AudioDataPlane* plane) noexcept : plane_(plane) {
                entered_ = plane_ != nullptr && plane_->tryEnter();
            }

            ~AccessScope() {
                if (entered_) {
                    plane_->leave();
                }
            }

            explicit operator bool() const noexcept {
                return entered_;
            }

         private:
            AudioDataPlane* plane_ = nullptr;
            bool entered_ = false;
        };

        bool tryEnter() noexcept {
            auto state = accessState.load(std::memory_order_relaxed);
            while ((state & kClosed) == 0) {
                if ((state & kCountMask) == kCountMask) {
                    return false;
                }
                if (accessState.compare_exchange_weak(state, state + 1,
                                                       std::memory_order_acquire,
                                                       std::memory_order_relaxed)) {
                    return true;
                }
            }
            return false;
        }

        void leave() noexcept {
            const auto previous = accessState.fetch_sub(1, std::memory_order_release);
            if ((previous & kCountMask) == 1) {
                waitCondition.notify_all();
            }
        }

        void closeAndWait() noexcept {
            accessState.fetch_or(kClosed, std::memory_order_acq_rel);
            std::unique_lock lock(waitMutex);
            waitCondition.wait(lock, [this] {
                return (accessState.load(std::memory_order_acquire) & kCountMask) == 0;
            });
        }

        void open() noexcept {
            accessState.fetch_and(kCountMask, std::memory_order_release);
        }

        SPSCRingBuffer<uint8_t, kAudioBufferSize> buffer;
        std::atomic<uint32_t> accessState{0};
        std::mutex waitMutex;
        std::condition_variable waitCondition;
        std::atomic<uint64_t> renderedDataLength{0};
        std::atomic<uint64_t> offsetTimeUs{0};
        std::atomic<bool> decoderCompleted{false};
    };

    void init() noexcept;

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
