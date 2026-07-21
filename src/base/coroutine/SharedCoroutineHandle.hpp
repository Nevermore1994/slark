//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "CoroutineControlBlock.hpp"

#include <atomic>
#include <coroutine>
#include <memory>
#include <utility>

namespace slark {

// 每个 await 注册创建一个恢复令牌。完成源共享同一令牌，且只能唤醒一次。
class SharedCoroutineHandle {
public:
    SharedCoroutineHandle() = default;

    explicit SharedCoroutineHandle(std::shared_ptr<CoroutineControlBlock> control) noexcept
        : control_(std::move(control)) {}

    SharedCoroutineHandle(SharedCoroutineHandle&& other) noexcept
        : control_(std::move(other.control_)),
          state_(other.state_.load(std::memory_order_acquire)),
          resumed_(other.resumed_.load(std::memory_order_acquire)) {}

    SharedCoroutineHandle& operator=(SharedCoroutineHandle&& other) noexcept {
        if (this != &other) {
            control_ = std::move(other.control_);
            state_.store(other.state_.load(std::memory_order_acquire), std::memory_order_release);
            resumed_.store(other.resumed_.load(std::memory_order_acquire), std::memory_order_release);
        }
        return *this;
    }

    SharedCoroutineHandle(const SharedCoroutineHandle&) = delete;
    SharedCoroutineHandle& operator=(const SharedCoroutineHandle&) = delete;

    // 由 await_suspend 在所有等待源注册完成后调用。
    [[nodiscard]] bool suspend() noexcept {
        auto expected = State::Registering;
        if (state_.compare_exchange_strong(expected, State::Suspended, std::memory_order_acq_rel)) {
            return true;
        }
        if (expected == State::Signaled) {
            state_.store(State::Scheduled, std::memory_order_release);
        }
        return false;
    }

    // 由事件、定时器、取消或子协程完成调用；返回 true 时调用方负责投递 scheduler。
    [[nodiscard]] bool notify() noexcept {
        auto expected = State::Registering;
        if (state_.compare_exchange_strong(expected, State::Signaled, std::memory_order_acq_rel)) {
            return false;
        }
        expected = State::Suspended;
        return state_.compare_exchange_strong(expected, State::Scheduled, std::memory_order_acq_rel);
    }

    bool tryResume() noexcept {
        bool expected = false;
        if (!resumed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel) || done()) {
            return false;
        }
        if (control_) {
            control_->resume();
        }
        return true;
    }

    [[nodiscard]] bool done() const noexcept {
        return control_ ? control_->isDone() : true;
    }

    [[nodiscard]] std::coroutine_handle<> handle() const noexcept {
        return control_ ? control_->handle() : std::coroutine_handle<>{};
    }

    [[nodiscard]] bool hasLifetime() const noexcept { return control_ != nullptr; }

    [[nodiscard]] std::shared_ptr<CoroutineControlBlock> controlBlock() const noexcept {
        return control_;
    }

private:
    enum class State : uint8_t { Registering, Signaled, Suspended, Scheduled };

    std::shared_ptr<CoroutineControlBlock> control_;
    std::atomic<State> state_{State::Registering};
    std::atomic<bool> resumed_{false};
};

} // namespace slark
