//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <functional>

namespace slark {

template <typename Level>
struct WatermarkConfig {
    Level low{};
    Level high{};
};

// The single hysteresis decision shared by every backpressure point in the pipeline.
// A gate closes at high and only reopens at low, so the band between them is a dead zone
// that keeps the current state. Misconfigured watermarks (high <= low) degrade to "keep
// the current state" instead of toggling on every sample.
template <typename Level>
[[nodiscard]] bool shouldCloseGate(Level current, Level low, Level high, bool closed) noexcept {
    if (high <= low) {
        return closed;
    }
    if (closed) {
        return current > low;
    }
    return current >= high;
}

// Hysteresis gate driven by two independent paths:
//   - watermark: update() closes at high (edge triggered) and opens at low. Opening is a
//     level signal, so capacity may be re-announced on every sample below the low watermark.
//   - operation: forceClose()/forceOpen() for seek / track switch / stop. A watermark sample
//     never releases an operation pause, so update() is a no-op while forced.
//
// reset() drops the watermark state only and never touches the delegate: releasing an
// operation pause is always an explicit forceOpen().
//
// The two flags are atomic because the paths can run on different threads (the decoder
// worker samples while the owner thread pauses) and each flag is written by only one of them.
template <typename Level>
class WatermarkGate {
 public:
    using Action = std::function<void()>;

    struct Actions {
        Action closeGate;
        Action openGate;
        Action interrupt;
        Action resume;
    };

    WatermarkGate(WatermarkConfig<Level> config, Actions actions) noexcept
        : config_(config), actions_(std::move(actions)) {}

    void update(Level current) noexcept {
        if (forced_.load(std::memory_order_acquire) || config_.high <= config_.low) {
            return;
        }
        if (shouldCloseGate(current, config_.low, config_.high,
                            closed_.load(std::memory_order_acquire))) {
            if (!closed_.exchange(true, std::memory_order_acq_rel)) {
                notify(actions_.closeGate);
            }
            return;
        }
        if (current <= config_.low) {
            closed_.store(false, std::memory_order_release);
            notify(actions_.openGate);
        }
    }

    void forceClose() noexcept {
        if (forced_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        closed_.store(false, std::memory_order_release);
        notify(actions_.interrupt);
    }

    void forceOpen() noexcept {
        if (!forced_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        closed_.store(false, std::memory_order_release);
        notify(actions_.resume);
        notify(actions_.openGate);
    }

    void reset() noexcept {
        closed_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool isClosed() const noexcept {
        return closed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isForced() const noexcept {
        return forced_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isPaused() const noexcept {
        return isClosed() || isForced();
    }

 private:
    static void notify(const Action& action) noexcept {
        if (action) {
            action();
        }
    }

    WatermarkConfig<Level> config_;
    Actions actions_;
    std::atomic<bool> closed_{false};
    std::atomic<bool> forced_{false};
};

}  // namespace slark
