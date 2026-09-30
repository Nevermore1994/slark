//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "SourceFlowController.h"

namespace slark {

SourceFlowController::SourceFlowController(const SourceBackpressureConfig& config,
                                           ISourceFlowDelegate& delegate) noexcept
    : gate_(WatermarkConfig<double>{config.lowWatermarkSec, config.highWatermarkSec},
            WatermarkGate<double>::Actions{
                .closeGate = [d = &delegate] { d->closeFetchGate(); },
                .openGate = [d = &delegate] { d->openFetchGate(); },
                .interrupt = [d = &delegate] { d->interruptStream(); },
                .resume = [d = &delegate] { d->resumeStream(); },
            }) {}

void SourceFlowController::update(double bufferedSec) noexcept {
    if (sourceEof_) {
        return;
    }
    gate_.update(bufferedSec);
}

void SourceFlowController::pauseForOperation() noexcept {
    gate_.forceClose();
}

void SourceFlowController::resumeForOperation() noexcept {
    gate_.forceOpen();
}

void SourceFlowController::reset() noexcept {
    gate_.reset();
    sourceEof_ = false;
}

bool SourceFlowController::isOperationPaused() const noexcept {
    return gate_.isForced();
}

bool SourceFlowController::isProductionSuspended() const noexcept {
    return gate_.isClosed();
}

bool SourceFlowController::isFetchAllowed() const noexcept {
    return !gate_.isPaused();
}

}  // namespace slark
