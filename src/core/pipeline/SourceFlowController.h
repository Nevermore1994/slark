//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "PipelineConfig.h"

namespace slark {

struct ISourceFlowDelegate {
    virtual ~ISourceFlowDelegate() = default;

    virtual void closeFetchGate() noexcept = 0;
    virtual void openFetchGate() noexcept = 0;

    virtual void interruptStream() noexcept = 0;
    virtual void resumeStream() noexcept = 0;
};

class SourceFlowController {
 public:
    SourceFlowController(const SourceBackpressureConfig& config,
                         ISourceFlowDelegate& delegate) noexcept;

    void update(double bufferedSec) noexcept;

    void setSourceEof() noexcept {
        sourceEof_ = true;
    }

    void pauseForOperation() noexcept;

    void resumeForOperation() noexcept;

    void reset() noexcept;

    [[nodiscard]] bool isOperationPaused() const noexcept {
        return operationPaused_;
    }

    [[nodiscard]] bool isProductionSuspended() const noexcept {
        return productionSuspended_;
    }

 private:
    SourceBackpressureConfig config_;
    ISourceFlowDelegate& delegate_;
    bool productionSuspended_ = false;
    bool operationPaused_ = false;
    bool sourceEof_ = false;
};

}  // namespace slark
