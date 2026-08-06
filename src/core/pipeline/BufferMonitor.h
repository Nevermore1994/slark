//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <chrono>
#include <functional>
#include <optional>

#include "PipelineConfig.h"
#include "PipelineError.h"
#include "PipelineEvent.h"
#include "Time.hpp"

namespace slark {

struct IBufferMonitorDelegate {
    virtual ~IBufferMonitorDelegate() = default;

    [[nodiscard]] virtual double demuxedDuration() const noexcept = 0;
    virtual void pauseStream() noexcept = 0;
    virtual void resumeStream() noexcept = 0;
    [[nodiscard]] virtual int32_t audioDecodedFrameCount() const noexcept = 0;
    [[nodiscard]] virtual int32_t videoDecodedFrameCount() const noexcept = 0;
    virtual void pauseAudioDecoder() noexcept = 0;
    virtual void resumeAudioDecoder() noexcept = 0;
    virtual void pauseVideoDecoder() noexcept = 0;
    virtual void resumeVideoDecoder() noexcept = 0;
    virtual void onPrebufferComplete() noexcept = 0;
    virtual void onDropOldest() noexcept = 0;
    virtual void onBufferEvent(PipelineEvent event,
                               std::optional<PipelineError> error) noexcept = 0;
};

class BufferMonitor {
 public:
    BufferMonitor(const BackpressureConfig& config, IBufferMonitorDelegate& delegate) noexcept;

    void onDemuxedDataEnqueued() noexcept;

    void onDecodedDataEnqueued() noexcept;

    void onHeartbeat() noexcept;

    void startPrebuffering() noexcept;

    void reset() noexcept;

    void pauseBackpressure() noexcept {
        backpressurePaused_ = true;
        resumeStream();
    }

    void resumeBackpressure() noexcept {
        backpressurePaused_ = false;
    }

    void setDemuxEof() noexcept {
        demuxEof_ = true;
        if (isRebuffering_) {
            isRebuffering_ = false;
            fireEvent(PipelineEvent::BufferingEnd);
        }
    }

    void setDecoderEof() noexcept {
        decoderEof_ = true;
    }

    void onStarvation(bool isLocalSource) noexcept;

    void resetStarvationCount() noexcept {
        starvationCount_ = 0;
    }

    [[nodiscard]] bool isPrebuffering() const noexcept {
        return isPrebuffering_;
    }

    [[nodiscard]] bool isRebuffering() const noexcept {
        return isRebuffering_;
    }

 private:
    void regulateDataFlow() noexcept;
    void regulateDecoderFlow() noexcept;
    void checkPrebufferComplete() noexcept;
    void checkRebufferComplete() noexcept;

    [[nodiscard]] double demuxedDuration() const noexcept {
        return delegate_.demuxedDuration();
    }

    void pauseStream() noexcept {
        if (!streamPaused_) {
            delegate_.pauseStream();
            streamPaused_ = true;
        }
    }

    void resumeStream() noexcept {
        if (streamPaused_) {
            delegate_.resumeStream();
            streamPaused_ = false;
        }
    }

    void pauseAudioDecoder() noexcept {
        if (!audioDecoderPaused_) {
            delegate_.pauseAudioDecoder();
            audioDecoderPaused_ = true;
        }
    }

    void resumeAudioDecoder() noexcept {
        if (audioDecoderPaused_) {
            delegate_.resumeAudioDecoder();
            audioDecoderPaused_ = false;
        }
    }

    void pauseVideoDecoder() noexcept {
        if (!videoDecoderPaused_) {
            delegate_.pauseVideoDecoder();
            videoDecoderPaused_ = true;
        }
    }

    void resumeVideoDecoder() noexcept {
        if (videoDecoderPaused_) {
            delegate_.resumeVideoDecoder();
            videoDecoderPaused_ = false;
        }
    }

    void fireEvent(PipelineEvent event,
                   std::optional<PipelineError> error = std::nullopt) noexcept {
        delegate_.onBufferEvent(event, std::move(error));
    }

 private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::steady_clock::time_point;

    static constexpr int32_t kStarvationThreshold = 5;

    BackpressureConfig config_;
    IBufferMonitorDelegate& delegate_;

    // demux-side state
    bool streamPaused_ = false;
    bool demuxEof_ = false;
    bool backpressurePaused_ = false;  // skip backpressure during fast frame pushing

    bool audioDecoderPaused_ = false;
    bool videoDecoderPaused_ = false;
    bool decoderEof_ = false;

    bool isPrebuffering_ = false;
    bool isRebuffering_ = false;
    bool readyFired_ = false;

    int32_t starvationCount_ = 0;

    std::optional<TimePoint> lastCheckTime_;
    std::optional<TimePoint> lastDecoderCheckTime_;
};

}  // namespace slark
