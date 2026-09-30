//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <optional>

#include "PipelineConfig.h"
#include "PipelineError.h"
#include "PipelineEvent.h"

namespace slark {

struct IBufferingTrackerDelegate {
    virtual ~IBufferingTrackerDelegate() = default;

    virtual void onPrebufferComplete() noexcept = 0;
    virtual void onBufferEvent(PipelineEvent event,
                               std::optional<PipelineError> error) noexcept = 0;
};

class BufferingTracker {
 public:
    BufferingTracker(const BufferingConfig& config, IBufferingTrackerDelegate& delegate) noexcept;

    void update(double bufferedSec) noexcept;

    void startPrebuffering() noexcept;

    void setSourceEof() noexcept {
        sourceEof_ = true;
        if (isRebuffering_) {
            isRebuffering_ = false;
            fireEvent(PipelineEvent::BufferingEnd);
        }
    }

    void reportStarvation(bool isLocalSource) noexcept;

    void resetStarvationCount() noexcept {
        starvationCount_ = 0;
    }

    void reset() noexcept;

    [[nodiscard]] bool isPrebuffering() const noexcept {
        return isPrebuffering_;
    }

    [[nodiscard]] bool isRebuffering() const noexcept {
        return isRebuffering_;
    }

 private:
    void checkPrebufferComplete(double duration) noexcept;
    void checkRebufferComplete(double duration) noexcept;
    void fireEvent(PipelineEvent event, std::optional<PipelineError> error = std::nullopt) noexcept;

 private:
    BufferingConfig config_;
    IBufferingTrackerDelegate& delegate_;

    bool sourceEof_ = false;
    bool isPrebuffering_ = false;
    bool isRebuffering_ = false;
    bool readyFired_ = false;
    int32_t starvationCount_ = 0;
};

}  // namespace slark
