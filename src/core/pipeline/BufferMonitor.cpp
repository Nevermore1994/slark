//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "BufferMonitor.h"

#include "Log.hpp"

namespace slark {

BufferMonitor::BufferMonitor(const BackpressureConfig& config,
                             PipelineEventCallback onEvent,
                             DemuxedDurationFunc demuxedDuration,
                             StreamControlFunc pauseStream,
                             StreamControlFunc resumeStream,
                             PrebufferCompleteFunc onPrebufferComplete,
                             DropOldestFunc dropOldest) noexcept
    : config_(config),
      onEvent_(std::move(onEvent)),
      demuxedDuration_(std::move(demuxedDuration)),
      pauseStream_(std::move(pauseStream)),
      resumeStream_(std::move(resumeStream)),
      onPrebufferComplete_(std::move(onPrebufferComplete)),
      dropOldest_(std::move(dropOldest)) {}

void BufferMonitor::onDataEnqueued() noexcept {
    const auto throttleMs =
        config_.isLiveSource ? 50 : (config_.startBufferDuration == 0.0 ? 100 : 50);
    const auto now = Clock::now();
    if (lastCheckTime_.has_value()) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastCheckTime_).count();
        if (elapsed < throttleMs) {
            return;
        }
    }
    lastCheckTime_ = now;

    if (isPrebuffering_) {
        checkPrebufferComplete();
        return;
    }
    regulateDataFlow();
}

void BufferMonitor::onHeartbeat() noexcept {
    if (!streamPaused_) {
        return;
    }
    const double duration = demuxedDuration();
    const auto now = Clock::now();
    const bool canResume =
        pauseTime_.has_value() &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - *pauseTime_).count() >=
            config_.minPauseHoldMs;
    if (duration <= config_.lowWatermarkDuration && canResume) {
        resumeStream();
        if (isRebuffering_) {
            checkRebufferComplete();
        }
    }
}

void BufferMonitor::startPrebuffering() noexcept {
    isPrebuffering_ = true;
    readyFired_ = false;
    if (config_.startBufferDuration <= 0.0) {
        isPrebuffering_ = false;
        readyFired_ = true;
        if (onPrebufferComplete_) {
            onPrebufferComplete_();
        }
    }
}

void BufferMonitor::reset() noexcept {
    streamPaused_ = false;
    isPrebuffering_ = false;
    isRebuffering_ = false;
    readyFired_ = false;
    pauseFailCount_ = 0;
    starvationCount_ = 0;
    pauseTime_.reset();
    resumeTime_.reset();
    lastCheckTime_.reset();
}

void BufferMonitor::regulateDataFlow() noexcept {
    const double duration = demuxedDuration();
    const auto now = Clock::now();

    if (duration > config_.hardCapDuration) {
        pauseStream();
        if (dropOldest_) {
            dropOldest_();
        }
        // hard-cap 收敛期间通知上层进入缓冲态
        if (!isRebuffering_ && readyFired_) {
            isRebuffering_ = true;
            fireEvent(PipelineEvent::BufferingStart);
        }
        return;
    }

    const bool canPause =
        !streamPaused_ &&
        (!resumeTime_.has_value() ||
         std::chrono::duration_cast<std::chrono::milliseconds>(now - *resumeTime_).count() >=
             config_.minResumeHoldMs);
    const bool canResume =
        streamPaused_ && pauseTime_.has_value() &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - *pauseTime_).count() >=
            config_.minPauseHoldMs;

    if (duration >= config_.highWatermarkDuration && canPause) {
        pauseStream();
    } else if (duration <= config_.lowWatermarkDuration) {
        if (!streamPaused_ && !isRebuffering_ && readyFired_) {
            isRebuffering_ = true;
            fireEvent(PipelineEvent::BufferingStart);
        }
        if (canResume) {
            resumeStream();
            checkRebufferComplete();
        }
    }

    // pause 失效兜底：连续多次超时后上报错误
    if (streamPaused_ && pauseTime_.has_value() && duration > config_.highWatermarkDuration) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - *pauseTime_).count();
        if (elapsed > 500) {
            ++pauseFailCount_;
            pauseTime_ = now;
            if (pauseFailCount_ >= kPauseFailThreshold) {
                fireEvent(PipelineEvent::ReadError,
                          PipelineError{
                              .event = PipelineEvent::ReadError,
                              .stage = PipelineStage::BackPressure,
                              .code = -1,
                              .message = "stream pause not effective after retries",
                          });
                pauseFailCount_ = 0;
            }
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
            if (onPrebufferComplete_) {
                onPrebufferComplete_();
            }
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
