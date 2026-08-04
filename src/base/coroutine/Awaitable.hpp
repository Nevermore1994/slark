// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

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
        if constexpr (std::is_void_v<T>) {
            return;
        } else {
            return std::move(*result_);
        }
    }

    template <typename U>
    void complete(U value) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(!std::is_void_v<T> && std::is_convertible_v<U, T>)
    {
        finish([&] {
            result_.emplace(T(std::move(value)));
        });
    }

    void complete() noexcept
        requires std::is_void_v<T>
    {
        finish([] {
        });
    }

    void fail(std::exception_ptr exception) noexcept {
        finish([&] {
            exception_ = std::move(exception);
        });
    }

 private:
    enum State : uint8_t { Init, Installing, Waiting, Completed };

    using ResultType = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

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
    std::optional<ResultType> result_;
    std::exception_ptr exception_;
    std::shared_ptr<ResumeGate> gate_;
    std::atomic<State> state_{Init};
};

}  // namespace slark
