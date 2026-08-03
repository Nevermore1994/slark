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
    // 重缓冲期间持续检查退出条件，不依赖 streamPaused_（BufferingStart 可能未暂停流）
    checkRebufferComplete();
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
    // 首帧渲染前跳过背压控制，允许快速推帧
    if (!readyFired_) {
        return;
    }
    // 快速推帧期间跳过背压控制，保证 demuxer 不被水位线阻塞
    if (backpressurePaused_) {
        return;
    }
    double duration = demuxedDuration();

    if (demuxEof_) {
        return;
    }

    // 低于重新缓冲水位线时触发 BufferingStart
    // 必须在 pause/resume 之前检查，避免 resume 后 isRebuffering_ 被清除导致误触发
    auto rebufferThreshold = config_.startBufferDuration * config_.rebufferScale;
    if (duration <= rebufferThreshold) {
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
