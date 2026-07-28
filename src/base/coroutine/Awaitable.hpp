// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "AliveFlag.hpp"
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
        handle_ = handle;
        flag_ = AliveFlag{};

        auto weakFlag = flag_.weak();
        auto h = handle_;
        timerId_ = CoroutineScheduler::instance().scheduleCallback(duration_, [weakFlag, h]() {
            checkAlive(weakFlag, [h]() {
                h.resume();
            });
        });
        return true;
    }

    void await_resume() noexcept {}

 private:
    void cancel() noexcept {
        flag_.setDead();
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    std::chrono::milliseconds duration_;
    std::coroutine_handle<> handle_;
    AliveFlag flag_;
    TimerId timerId_;
};

template <typename T>
class CompletionAwaiter {
 public:
    ~CompletionAwaiter() {
        flag_.setDead();
    }

    bool await_ready() const noexcept {
        return state_.load(std::memory_order_acquire) != Init;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        flag_ = AliveFlag{};
        auto expected = Init;
        return !state_.compare_exchange_strong(expected, Waiting, std::memory_order_acq_rel);
    }

    T await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        return std::move(*result_);
    }

    void complete(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        result_.emplace(std::move(value));
        finish();
    }

    void fail(std::exception_ptr exception) noexcept {
        exception_ = std::move(exception);
        finish();
    }

 private:
    enum State : uint8_t { Init, Waiting, Completed };

    void finish() noexcept {
        auto expected = Waiting;
        if (state_.compare_exchange_strong(expected, Completed, std::memory_order_acq_rel)) {
            if (flag_.isAlive()) {
                handle_.resume();
            }
            return;
        }
        state_.store(Completed, std::memory_order_release);
    }

    std::coroutine_handle<> handle_;
    AliveFlag flag_;
    std::optional<T> result_;
    std::exception_ptr exception_;
    std::atomic<State> state_{Init};
};

template <>
class CompletionAwaiter<void> {
 public:
    ~CompletionAwaiter() {
        flag_.setDead();
    }

    bool await_ready() const noexcept {
        return state_.load(std::memory_order_acquire) != Init;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        flag_ = AliveFlag{};
        auto expected = Init;
        return !state_.compare_exchange_strong(expected, Waiting, std::memory_order_acq_rel);
    }

    void await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    void complete() noexcept {
        finish();
    }

    void fail(std::exception_ptr exception) noexcept {
        exception_ = std::move(exception);
        finish();
    }

 private:
    enum State : uint8_t { Init, Waiting, Completed };

    void finish() noexcept {
        auto expected = Waiting;
        if (state_.compare_exchange_strong(expected, Completed, std::memory_order_acq_rel)) {
            if (flag_.isAlive()) {
                handle_.resume();
            }
            return;
        }
        state_.store(Completed, std::memory_order_release);
    }

    std::coroutine_handle<> handle_;
    AliveFlag flag_;
    std::exception_ptr exception_;
    std::atomic<State> state_{Init};
};

}  // namespace slark
