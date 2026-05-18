//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "BufferMonitor.h"

#include "Log.hpp"

namespace slark {

BufferMonitor::BufferMonitor(const BackpressureConfig& config,
                             IBufferMonitorDelegate& delegate) noexcept
    : config_(config), delegate_(delegate) {}

void BufferMonitor::onDemuxedDataEnqueued() noexcept {
    using Ms = std::chrono::milliseconds;
    const auto throttle = config_.isLiveSource ? Ms(50) : Ms(100);
    auto now = Clock::now();
    if (lastCheckTime_.has_value() && (now - *lastCheckTime_) < throttle) {
        return;
    }
    lastCheckTime_ = now;

    if (isPrebuffering_) {
        checkPrebufferComplete();
        return;
    }
    regulateDataFlow();
}

void BufferMonitor::onDecodedDataEnqueued() noexcept {
    if (decoderEof_) {
        return;
    }
    constexpr auto kThrottle = std::chrono::milliseconds(50);
    auto now = Clock::now();
    if (lastDecoderCheckTime_.has_value() && (now - *lastDecoderCheckTime_) < kThrottle) {
        return;
    }
    lastDecoderCheckTime_ = now;
    regulateDecoderFlow();
}

void BufferMonitor::onHeartbeat() noexcept {
    if (!demuxEof_) {
        if (isPrebuffering_) {
            checkPrebufferComplete();
        } else {
            regulateDataFlow();
        }
    }
    if (!decoderEof_) {
        regulateDecoderFlow();
    }
}

void BufferMonitor::startPrebuffering() noexcept {
    isPrebuffering_ = true;
    readyFired_ = false;
    if (config_.startBufferDuration <= 0.0) {
        isPrebuffering_ = false;
        readyFired_ = true;
        delegate_.onPrebufferComplete();
    }
}

void BufferMonitor::reset() noexcept {
    streamPaused_ = false;
    demuxEof_ = false;
    audioDecoderPaused_ = false;
    videoDecoderPaused_ = false;
    decoderEof_ = false;
    isPrebuffering_ = false;
    isRebuffering_ = false;
    readyFired_ = false;
    pauseFailCount_ = 0;
    starvationCount_ = 0;
    lastCheckTime_.reset();
    lastDecoderCheckTime_.reset();
}

void BufferMonitor::regulateDecoderFlow() noexcept {
    if (decoderEof_) {
        return;
    }

    {
        int32_t frames = delegate_.audioDecodedFrameCount();
        if (frames >= config_.audioDecoderHighWatermarkFrames) {
            pauseAudioDecoder();
        } else if (frames <= config_.audioDecoderLowWatermarkFrames) {
            resumeAudioDecoder();
        }
    }

    {
        int32_t frames = delegate_.videoDecodedFrameCount();
        if (frames >= config_.videoDecoderHighWatermarkFrames) {
            pauseVideoDecoder();
        } else if (frames <= config_.videoDecoderLowWatermarkFrames) {
            resumeVideoDecoder();
        }
    }
}

void BufferMonitor::regulateDataFlow() noexcept {
    // 快速推帧期间跳过背压控制，保证 demuxer 不被水位线阻塞
    if (backpressurePaused_) {
        return;
    }
    double duration = demuxedDuration();

    if (demuxEof_) {
        return;
    }

    if (duration > config_.hardCapDuration) {
        pauseStream();
        delegate_.onDropOldest();
        if (!isRebuffering_ && readyFired_) {
            isRebuffering_ = true;
            fireEvent(PipelineEvent::BufferingStart);
        }
        return;
    }

    if (duration >= config_.demuxHighWatermarkDuration) {
        pauseStream();
    } else if (duration <= config_.demuxLowWatermarkDuration) {
        if (!streamPaused_ && !isRebuffering_ && readyFired_) {
            isRebuffering_ = true;
            fireEvent(PipelineEvent::BufferingStart);
        }
        if (streamPaused_) {
            resumeStream();
            checkRebufferComplete();
        }
    }

    // pause 失效屌底：连续多次超限后上报错误
    if (streamPaused_ && duration > config_.demuxHighWatermarkDuration) {
        ++pauseFailCount_;
        if (pauseFailCount_ >= kPauseFailThreshold) {
            fireEvent(PipelineEvent::ReadError, PipelineError{.error = Error::InvalidOperation});
            pauseFailCount_ = 0;
        }
    }
}

void BufferMonitor::checkPrebufferComplete() noexcept {
    if (!isPrebuffering_) {
        return;
    }
    if (demuxedDuration() >= config_.startBufferDuration) {
        isPrebuffering_ = false;
        if (!readyFired_) {
            readyFired_ = true;
            delegate_.onPrebufferComplete();
        }
    }
}

void BufferMonitor::checkRebufferComplete() noexcept {
    if (!isRebuffering_) {
        return;
    }
    if (demuxedDuration() >= config_.rebufferDuration) {
        isRebuffering_ = false;
        fireEvent(PipelineEvent::BufferingEnd);
    }
}

void BufferMonitor::onStarvation(bool isLocalSource) noexcept {
    if (isLocalSource) {
        return;
    }
    ++starvationCount_;
    if (starvationCount_ >= kStarvationThreshold && !isRebuffering_ && readyFired_) {
        isRebuffering_ = true;
        fireEvent(PipelineEvent::BufferingStart);
    }
}

} // namespace slark
