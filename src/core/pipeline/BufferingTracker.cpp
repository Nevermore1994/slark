//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "BufferingTracker.h"

#include "Log.hpp"

namespace slark {

BufferingTracker::BufferingTracker(const BufferingConfig& config,
                                   IBufferingTrackerDelegate& delegate) noexcept
    : config_(config), delegate_(delegate) {}

void BufferingTracker::update(double bufferedSec) noexcept {
    const bool isStarting = isPrebuffering_;

    if (!sourceEof_ && !isStarting) {
        // Guard against re-triggering on the sample that just cleared isRebuffering_.
        if (bufferedSec < config_.rebufferTargetSec() && !isRebuffering_ && readyFired_) {
            isRebuffering_ = true;
            LogI("start rebuffering:{}", bufferedSec);
            fireEvent(PipelineEvent::BufferingStart);
        }
    }

    if (isStarting) {
        checkPrebufferComplete(bufferedSec);
    }

    checkRebufferComplete(bufferedSec);
}

void BufferingTracker::startPrebuffering() noexcept {
    isPrebuffering_ = true;
    readyFired_ = false;
    if (config_.startBufferSec <= 0.0) {
        isPrebuffering_ = false;
        readyFired_ = true;
        delegate_.onPrebufferComplete();
    }
}

void BufferingTracker::reportStarvation(bool isLocalSource) noexcept {
    if (isLocalSource) {
        return;
    }
    ++starvationCount_;
    if (starvationCount_ >= config_.starvationThreshold && !isRebuffering_ && readyFired_) {
        isRebuffering_ = true;
        LogI("starvation -> BufferingStart, count={}", starvationCount_);
        fireEvent(PipelineEvent::BufferingStart);
    }
}

void BufferingTracker::reset() noexcept {
    sourceEof_ = false;
    isPrebuffering_ = false;
    isRebuffering_ = false;
    readyFired_ = false;
    starvationCount_ = 0;
}

void BufferingTracker::checkPrebufferComplete(double duration) noexcept {
    if (!isPrebuffering_) {
        return;
    }
    if (duration >= config_.startBufferSec) {
        isPrebuffering_ = false;
        if (!readyFired_) {
            readyFired_ = true;
            delegate_.onPrebufferComplete();
        }
    }
}

void BufferingTracker::checkRebufferComplete(double duration) noexcept {
    if (!isRebuffering_) {
        return;
    }
    if (duration >= config_.rebufferExitSec()) {
        isRebuffering_ = false;
        LogI("checkRebufferComplete -> BufferingEnd, duration={:.3f} threshold={:.3f}", duration,
             config_.rebufferExitSec());
        fireEvent(PipelineEvent::BufferingEnd);
    }
}

void BufferingTracker::fireEvent(PipelineEvent event, std::optional<PipelineError> error) noexcept {
    delegate_.onBufferEvent(event, std::move(error));
}

}  // namespace slark
