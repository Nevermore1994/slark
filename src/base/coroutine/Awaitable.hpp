//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "Coroutine.hpp"
#include "CoroutineScheduler.hpp"
#include "SharedCoroutineHandle.hpp"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace slark {

class TimerAwaiter {
public:
    explicit TimerAwaiter(std::chrono::milliseconds duration) noexcept : duration_(duration) {}

    bool await_ready() const noexcept { return duration_.count() <= 0; }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto shared = handle.promise().makeResumeHandle();
        if (!shared) {
            return false;
        }
        CoroutineScheduler::instance().scheduleCallback(duration_, [shared]() {
            if (shared->notify()) {
                CoroutineScheduler::instance().schedule(shared);
            }
        });
        return shared->suspend();
    }

    void await_resume() noexcept {}

private:
    std::chrono::milliseconds duration_;
};

template <typename T>
class CompletionAwaiter {
public:
    bool await_ready() const noexcept {
        return state_.load(std::memory_order_acquire) != State::Init;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto sharedHandle = handle.promise().makeResumeHandle();
        auto expected = State::Init;
        if (!state_.compare_exchange_strong(expected, State::Waiting, std::memory_order_acq_rel)) {
            // 已经在 await_suspend 和 await_ready 之间被 complete 了
            // 不挂起，直接继续执行 await_resume
            return false;
        }
        // 设置 handle_ 必须在 CAS 之后，避免 finish() 看到 handle_ 为 null
        handle_ = std::move(sharedHandle);
        return handle_ && handle_->suspend();
    }

    T await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        return std::move(*result_);
    }

    void complete(T result) noexcept(std::is_nothrow_move_constructible_v<T>) {
        result_.emplace(std::move(result));
        finish();
    }

    void fail(std::exception_ptr exception) noexcept {
        exception_ = std::move(exception);
        finish();
    }

private:
    enum class State : uint8_t { Init, Waiting, Completed };

    void finish() noexcept {
        auto expected = State::Waiting;
        if (state_.compare_exchange_strong(expected, State::Completed, std::memory_order_acq_rel)) {
            // await_suspend 已设置 handle_，可以直接 notify
            if (handle_ && handle_->notify()) {
                CoroutineScheduler::instance().schedule(handle_);
            }
            return;
        }
        // state 是 Init（await_suspend 还没执行），标记为 Completed
        // await_suspend 会看到 state != Init 并返回 false（不挂起）
        state_.store(State::Completed, std::memory_order_release);
    }

    std::shared_ptr<SharedCoroutineHandle> handle_;
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

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto sharedHandle = handle.promise().makeResumeHandle();
        auto expected = State::Init;
        if (!state_.compare_exchange_strong(expected, State::Waiting, std::memory_order_acq_rel)) {
            return false;
        }
        handle_ = std::move(sharedHandle);
        return handle_ && handle_->suspend();
    }

    void await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    void complete() noexcept { finish(); }

    void fail(std::exception_ptr exception) noexcept {
        exception_ = std::move(exception);
        finish();
    }

private:
    enum class State : uint8_t { Init, Waiting, Completed };

    void finish() noexcept {
        auto expected = State::Waiting;
        if (state_.compare_exchange_strong(expected, State::Completed, std::memory_order_acq_rel)) {
            if (handle_ && handle_->notify()) {
                CoroutineScheduler::instance().schedule(handle_);
            }
            return;
        }
        state_.store(State::Completed, std::memory_order_release);
    }

    std::shared_ptr<SharedCoroutineHandle> handle_;
    std::exception_ptr exception_;
    std::atomic<State> state_{State::Init};
};

} // namespace slark
