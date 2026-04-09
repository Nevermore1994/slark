//
// Created by Nevermore on 2025/4/1.
// Slark Awaitable
// Copyright (c) 2025 Nevermore All rights reserved.
//
#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <exception>
#include <functional>
#include <optional>

namespace slark {

class CoroutineScheduler;

class TimerAwaiter {
public:
    explicit TimerAwaiter(std::chrono::milliseconds duration) noexcept : duration_(duration) {}

    bool await_ready() const noexcept {
        return duration_.count() <= 0;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept;

    void await_resume() noexcept {}

private:
    std::chrono::milliseconds duration_;
};

// 用于等待外部异步操作完成的通用 awaiter
template <typename T>
class CompletionAwaiter {
public:
    bool await_ready() const noexcept {
        return state_.load(std::memory_order_acquire) != State::Init;
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept {
        handle_ = handle;
        auto expected = State::Init;
        // 如果 CAS 失败，说明 complete/fail 已先于 await_suspend 调用，不挂起
        return state_.compare_exchange_strong(expected, State::Waiting, std::memory_order_acq_rel);
    }

    T await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        return std::move(*result_);
    }

    void complete(T result) noexcept(std::is_nothrow_move_constructible_v<T>) {
        result_.emplace(std::move(result));
        auto expected = State::Waiting;
        if (state_.compare_exchange_strong(expected, State::Completed, std::memory_order_acq_rel)) {
            if (handle_) {
                handle_.resume();
            }
        } else {
            // await_suspend 还未调用，标记为已完成，await_ready 会返回 true
            state_.store(State::Completed, std::memory_order_release);
        }
    }

    void fail(std::exception_ptr ex) noexcept {
        exception_ = ex;
        auto expected = State::Waiting;
        if (state_.compare_exchange_strong(expected, State::Completed, std::memory_order_acq_rel)) {
            if (handle_) {
                handle_.resume();
            }
        } else {
            state_.store(State::Completed, std::memory_order_release);
        }
    }

private:
    enum class State : uint8_t { Init, Waiting, Completed };

    std::coroutine_handle<> handle_;
    std::optional<T> result_;
    std::exception_ptr exception_;
    std::atomic<State> state_{State::Init};
};

template <>
class CompletionAwaiter<void> {
public:
    bool await_ready() const noexcept {
        return state_.load(std::memory_order_acquire) != State::Init;
    }

    bool await_suspend(std::coroutine_handle<> handle) noexcept {
        handle_ = handle;
        auto expected = State::Init;
        return state_.compare_exchange_strong(expected, State::Waiting, std::memory_order_acq_rel);
    }

    void await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    void complete() noexcept {
        auto expected = State::Waiting;
        if (state_.compare_exchange_strong(expected, State::Completed, std::memory_order_acq_rel)) {
            if (handle_) {
                handle_.resume();
            }
        } else {
            state_.store(State::Completed, std::memory_order_release);
        }
    }

    void fail(std::exception_ptr ex) noexcept {
        exception_ = ex;
        auto expected = State::Waiting;
        if (state_.compare_exchange_strong(expected, State::Completed, std::memory_order_acq_rel)) {
            if (handle_) {
                handle_.resume();
            }
        } else {
            state_.store(State::Completed, std::memory_order_release);
        }
    }

private:
    enum class State : uint8_t { Init, Waiting, Completed };

    std::coroutine_handle<> handle_;
    std::exception_ptr exception_;
    std::atomic<State> state_{State::Init};
};

} // namespace slark
