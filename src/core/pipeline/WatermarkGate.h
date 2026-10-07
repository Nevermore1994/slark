//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <concepts>
#include <utility>

#include "MoveOnlyFunction.hpp"

namespace slark {

template <typename Level>
struct WatermarkConfig {
    Level low{};
    Level high{};
};

// Misconfigured watermarks (high <= low) keep the current state instead of toggling on every
// sample.
template <typename Level>
    requires std::totally_ordered<Level>
[[nodiscard]] constexpr bool shouldCloseGate(Level current, Level low, Level high,
                                             bool closed) noexcept {
    if (high <= low) {
        return closed;
    }
    if (closed) {
        return current > low;
    }
    return current >= high;
}

// Two independent paths drive the gate:
//   - watermark: update() closes at high (edge triggered) and opens at low (level signal).
//   - operation: forceClose()/forceOpen(). A watermark sample never releases an operation pause.
// reset() clears the watermark state only, so releasing an operation pause stays explicit.
// Each flag is written by one path and read by the other, hence the atomics.
template <typename Level>
    requires std::totally_ordered<Level>
class WatermarkGate {
 public:
    using Action = MoveOnlyFunction<void()>;

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
    static void notify(Action& action) noexcept {
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
