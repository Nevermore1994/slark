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

    checkDataFlow();
}

void BufferMonitor::onHeartbeat() noexcept {
    if (!demuxEof_) {
        checkDataFlow();
    }
    // Keep checking the exit condition during rebuffering, independent of streamPaused_
    // (BufferingStart may not have paused the stream)
    checkRebufferComplete();
    regulateDecoderFlow();
}

void BufferMonitor::checkDataFlow() noexcept {
    const double duration = demuxedDuration();
    const bool isStarting = isPrebuffering_;
    regulateDataFlow(duration, !isStarting);
    if (isStarting) {
        checkPrebufferComplete(duration);
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
    demuxEof_ = false;
    audioDecoderPaused_ = false;
    videoDecoderPaused_ = false;
    isPrebuffering_ = false;
    isRebuffering_ = false;
    readyFired_ = false;
    starvationCount_ = 0;
    lastCheckTime_.reset();
}

void BufferMonitor::regulateDecoderFlow() noexcept {
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

void BufferMonitor::regulateDataFlow(double duration, bool allowBufferingEvents) noexcept {
    if (demuxEof_) {
        return;
    }

    // Trigger BufferingStart when below the rebuffer watermark.
    // Must be checked before pause/resume so a resume clearing isRebuffering_ cannot
    // falsely re-trigger it.
    auto rebufferThreshold = config_.startBufferDuration * config_.rebufferScale;
    if (allowBufferingEvents && duration <= rebufferThreshold) {
        if (!isRebuffering_ && readyFired_) {
            isRebuffering_ = true;
            LogI("start rebuffering:{}", duration);
            fireEvent(PipelineEvent::BufferingStart);
        }
    }

    if (duration >= config_.demuxHighWatermarkDuration) {
        pauseStream();
    } else if (duration <= config_.demuxLowWatermarkDuration) {
        if (streamPaused_) {
            resumeStream();
            checkRebufferComplete();
        }
    }
}

void BufferMonitor::checkPrebufferComplete(double duration) noexcept {
    if (!isPrebuffering_) {
        return;
    }
    if (duration >= config_.startBufferDuration) {
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
    auto rebufferThreshold = config_.startBufferDuration * config_.rebufferScale;
    if (demuxedDuration() >= rebufferThreshold) {
        isRebuffering_ = false;
        LogI("checkRebufferComplete -> BufferingEnd, demuxedDuration={:.3f} threshold={:.3f}",
             demuxedDuration(), rebufferThreshold);
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
        LogI("onStarvation -> BufferingStart, starvationCount={}, demuxedDuration={:.3f}",
             starvationCount_, demuxedDuration());
        fireEvent(PipelineEvent::BufferingStart);
    }
}

}  // namespace slark
