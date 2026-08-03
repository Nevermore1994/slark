// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include "CoroutineControl.hpp"
#include "CoroutineScheduler.hpp"

namespace slark {

class TimerAwaiter {
 public:
    explicit TimerAwaiter(std::chrono::milliseconds duration) noexcept : duration_(duration) {}

    ~TimerAwaiter() {
        cancel();
    }

    bool await_ready() const noexcept {
        return duration_.count() <= 0;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        gate_ = makeResumeGate(handle);
        auto weakGate = std::weak_ptr<ResumeGate>(gate_);
        timerId_ = CoroutineScheduler::instance().scheduleCallback(duration_, [weakGate]() {
            if (auto gate = weakGate.lock()) {
                gate->signal();
            }
        });
        return gate_->arm();
    }

    void await_resume() noexcept {}

 private:
    void cancel() noexcept {
        if (gate_) {
            gate_->disarm();
        }
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    std::chrono::milliseconds duration_;
    std::shared_ptr<ResumeGate> gate_;
    TimerId timerId_;
};

template <typename T>
class CompletionAwaiter {
 public:
    ~CompletionAwaiter() {
        std::lock_guard lock(mutex_);
        if (gate_) {
            gate_->disarm();
        }
    }

    bool await_ready() const noexcept {
        return state_.load(std::memory_order_acquire) == Completed;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto gate = makeResumeGate(handle);
        {
            std::lock_guard lock(mutex_);
            gate_ = gate;
            auto expected = Init;
            if (!state_.compare_exchange_strong(expected, Installing, std::memory_order_acq_rel)) {
                gate_->disarm();
                return false;
            }
        }

        std::lock_guard lock(mutex_);
        auto expected = Installing;
        if (state_.compare_exchange_strong(expected, Waiting, std::memory_order_acq_rel)) {
            return gate_->arm();
        }
        gate_->disarm();
        return false;
    }

    T await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        return std::move(*result_);
    }

    void complete(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        finish([&] { result_.emplace(std::move(value)); });
    }

    void fail(std::exception_ptr exception) noexcept {
        finish([&] { exception_ = std::move(exception); });
    }

 private:
    enum State : uint8_t { Init, Installing, Waiting, Completed };

    template <typename Publish>
    void finish(Publish&& publish) noexcept {
        std::shared_ptr<ResumeGate> gate;
        bool shouldSignal = false;
        {
            std::lock_guard lock(mutex_);
            auto state = state_.load(std::memory_order_relaxed);
            if (state == Completed) {
                return;
            }
            publish();
            state_.store(Completed, std::memory_order_release);
            shouldSignal = state == Installing || state == Waiting;
            gate = gate_;
        }
        if (shouldSignal && gate) {
            gate->signal();
        }
    }

    mutable std::mutex mutex_;
    std::optional<T> result_;
    std::exception_ptr exception_;
    std::shared_ptr<ResumeGate> gate_;
    std::atomic<State> state_{Init};
};

template <>
class CompletionAwaiter<void> {
 public:
    ~CompletionAwaiter() {
        std::lock_guard lock(mutex_);
        if (gate_) {
            gate_->disarm();
        }
    }

    bool await_ready() const noexcept {
        return state_.load(std::memory_order_acquire) == Completed;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto gate = makeResumeGate(handle);
        {
            std::lock_guard lock(mutex_);
            gate_ = gate;
            auto expected = Init;
            if (!state_.compare_exchange_strong(expected, Installing, std::memory_order_acq_rel)) {
                gate_->disarm();
                return false;
            }
        }

        std::lock_guard lock(mutex_);
        auto expected = Installing;
        if (state_.compare_exchange_strong(expected, Waiting, std::memory_order_acq_rel)) {
            return gate_->arm();
        }
        gate_->disarm();
        return false;
    }

    void await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    void complete() noexcept {
        finish([] {});
    }

    void fail(std::exception_ptr exception) noexcept {
        finish([&] { exception_ = std::move(exception); });
    }

 private:
    enum State : uint8_t { Init, Installing, Waiting, Completed };

    template <typename Publish>
    void finish(Publish&& publish) noexcept {
        std::shared_ptr<ResumeGate> gate;
        bool shouldSignal = false;
        {
            std::lock_guard lock(mutex_);
            auto state = state_.load(std::memory_order_relaxed);
            if (state == Completed) {
                return;
            }
            publish();
            state_.store(Completed, std::memory_order_release);
            shouldSignal = state == Installing || state == Waiting;
            gate = gate_;
        }
        if (shouldSignal && gate) {
            gate->signal();
        }
    }

    mutable std::mutex mutex_;
    std::exception_ptr exception_;
    std::shared_ptr<ResumeGate> gate_;
    std::atomic<State> state_{Init};
};

}  // namespace slark
