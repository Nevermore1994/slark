//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace slark {

/// Reader gate: many threads enter via Scope, one owner closes it and waits for all to leave.
class AccessGate {
public:
    class Scope {
    public:
        explicit Scope(AccessGate* gate) noexcept : gate_(gate) {
            entered_ = gate_ != nullptr && gate_->tryEnter();
        }

        ~Scope() {
            if (entered_) {
                gate_->leave();
            }
        }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        explicit operator bool() const noexcept {
            return entered_;
        }

    private:
        AccessGate* gate_ = nullptr;
        bool entered_ = false;
    };

    explicit AccessGate(bool initiallyOpen = true) noexcept
        : state_(initiallyOpen ? 0 : kClosed) {}

    bool tryEnter() noexcept {
        auto state = state_.load(std::memory_order_relaxed);
        while ((state & kClosed) == 0) {
            if ((state & kCountMask) == kCountMask) {
                return false;
            }
            if (state_.compare_exchange_weak(state, state + 1, std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    void leave() noexcept {
        const auto previous = state_.fetch_sub(1, std::memory_order_release);
        if ((previous & kClosed) != 0 && (previous & kCountMask) == 1) {
            // Empty critical section orders this notify after the waiter's predicate check.
            { std::lock_guard lock(mutex_); }
            cond_.notify_all();
        }
    }

    void closeAndWait() noexcept {
        state_.fetch_or(kClosed, std::memory_order_acq_rel);
        std::unique_lock lock(mutex_);
        cond_.wait(lock, [this] {
            return (state_.load(std::memory_order_acquire) & kCountMask) == 0;
        });
    }

    void open() noexcept {
        state_.fetch_and(kCountMask, std::memory_order_release);
    }

    [[nodiscard]] bool isClosed() const noexcept {
        return (state_.load(std::memory_order_acquire) & kClosed) != 0;
    }

private:
    static constexpr uint32_t kClosed = 1U << 31;
    static constexpr uint32_t kCountMask = ~kClosed;

    std::atomic<uint32_t> state_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

}  // namespace slark
