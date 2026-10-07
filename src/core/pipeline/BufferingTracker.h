//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <optional>

#include "FlowConfig.h"
#include "PipelineTypes.h"

namespace slark {

struct IBufferingTrackerDelegate {
    virtual ~IBufferingTrackerDelegate() = default;

    virtual void onPrebufferComplete() noexcept = 0;
    virtual void onBufferEvent(PipelineEvent event,
                               std::optional<PipelineError> error) noexcept = 0;
};

// Buffering state belongs to the pipeline owner thread. The render thread only announces
// starvation through signalStarvation() and resetStarvationCount(); every threshold decision
// and event stays on the owner thread.
class BufferingTracker {
 public:
    BufferingTracker(const BufferingConfig& config, IBufferingTrackerDelegate& delegate,
                     bool isLiveSource = false) noexcept;

    void update(double bufferedSec) noexcept;

    void startPrebuffering() noexcept;

    void setSourceEof() noexcept {
        sourceEof_ = true;
        if (isRebuffering_) {
            isRebuffering_ = false;
            fireEvent(PipelineEvent::BufferingEnd);
        }
    }

    // Render thread entry: one announcement per frame it found nothing to draw. Counted, not
    // flagged, so a tick that starved several times still accounts for each of them.
    void signalStarvation() noexcept {
        starvationSignals_.fetch_add(1, std::memory_order_relaxed);
    }

    void resetStarvationCount() noexcept {
        starvationCount_.store(0, std::memory_order_relaxed);
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
    void consumeStarvationSignals() noexcept;
    void fireEvent(PipelineEvent event, std::optional<PipelineError> error = std::nullopt) noexcept;

 private:
    BufferingConfig config_;
    IBufferingTrackerDelegate& delegate_;
    const bool kIsLiveSource;

    bool sourceEof_ = false;
    bool isPrebuffering_ = false;
    bool isRebuffering_ = false;
    bool readyFired_ = false;
    std::atomic<int32_t> starvationCount_{0};
    std::atomic<int32_t> starvationSignals_{0};
};

}  // namespace slark
