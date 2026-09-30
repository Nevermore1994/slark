//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "PipelineConfig.h"
#include "WatermarkGate.h"

namespace slark {

struct ISourceFlowDelegate {
    virtual ~ISourceFlowDelegate() = default;

    virtual void closeFetchGate() noexcept = 0;
    virtual void openFetchGate() noexcept = 0;

    virtual void interruptStream() noexcept = 0;
    virtual void resumeStream() noexcept = 0;
};

// Regulates the source from the demuxed buffer level. All hysteresis lives in the gate;
// this class only maps gate actions onto the delegate and tracks source EOF.
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

    [[nodiscard]] bool isOperationPaused() const noexcept;

    [[nodiscard]] bool isProductionSuspended() const noexcept;

    // What the source gate answers: no production while suspended by a watermark or held by
    // an operation pause.
    [[nodiscard]] bool isFetchAllowed() const noexcept;

 private:
    WatermarkGate<double> gate_;
    bool sourceEof_ = false;
};

}  // namespace slark
