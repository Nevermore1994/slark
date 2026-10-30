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

template <typename Level>
    requires std::totally_ordered<Level>
[[nodiscard]] constexpr bool shouldCloseGate(Level current,
                                             Level low,
                                             Level high,
                                             bool closed) noexcept {
    if (high <= low) {
        return closed;
    }
    if (closed) {
        return current > low;
    }
    return current >= high;
}

// forceClose/forceOpen own the gate: a watermark sample never releases an operation pause.
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
        if (forced_.load(std::memory_order_acquire) || held_.load(std::memory_order_acquire) ||
            config_.high <= config_.low) {
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

    // Ignores the watermark until releaseHold(); a frozen seek clock makes the level look full.
    void holdOpen() noexcept {
        if (held_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        if (!closed_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        notify(actions_.openGate);
    }

    void releaseHold() noexcept {
        held_.store(false, std::memory_order_release);
    }

    // An operation pause (forced_) owns the gate and is left untouched.
    void reset() noexcept {
        if (forced_.load(std::memory_order_acquire)) {
            return;
        }
        if (closed_.exchange(false, std::memory_order_acq_rel) && actions_.openGate) {
            actions_.openGate();
        }
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

    [[nodiscard]] bool isHeld() const noexcept {
        return held_.load(std::memory_order_acquire);
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
    std::atomic<bool> held_{false};
};

}  // namespace slark
