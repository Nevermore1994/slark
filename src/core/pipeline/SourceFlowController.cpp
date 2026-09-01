//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "SourceFlowController.h"

namespace slark {

SourceFlowController::SourceFlowController(const SourceBackpressureConfig& config,
                                           ISourceFlowDelegate& delegate) noexcept
    : config_(config), delegate_(delegate) {}

void SourceFlowController::update(double bufferedSec) noexcept {
    if (sourceEof_ || operationPaused_) {
        return;
    }
    if (bufferedSec >= config_.highWatermarkSec) {
        if (!productionSuspended_) {
            productionSuspended_ = true;
            delegate_.closeFetchGate();
        }
    } else if (bufferedSec <= config_.lowWatermarkSec) {
        if (productionSuspended_) {
            productionSuspended_ = false;
        }
        delegate_.openFetchGate();
    }
}

void SourceFlowController::pauseForOperation() noexcept {
    if (!operationPaused_) {
        operationPaused_ = true;
        productionSuspended_ = false;
        delegate_.interruptStream();
    }
}

void SourceFlowController::resumeForOperation() noexcept {
    if (operationPaused_) {
        operationPaused_ = false;
        delegate_.resumeStream();
        delegate_.openFetchGate();
    }
}

void SourceFlowController::reset() noexcept {
    // operationPaused_ tracks a real delegate call; leaving it set would short-circuit
    // the next pauseForOperation().
    productionSuspended_ = false;
    operationPaused_ = false;
    sourceEof_ = false;
}

}  // namespace slark
