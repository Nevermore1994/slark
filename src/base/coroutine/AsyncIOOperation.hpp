//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "CoroutineScheduler.hpp"
#include "Error.h"
#include "Timer.h"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <expected>
#include <memory>
#include <optional>

namespace slark {

template <typename T>
using IOResult = std::expected<T, Error>;

namespace detail {

enum class CompletionState : uint8_t {
    Pending = 0,
    Completed,
    Failed,
    TimedOut,
};

// 独立的状态结构，生命周期由 shared_ptr 管理，避免 UAF
template <typename T>
struct IOState {
    std::atomic<CompletionState> state{CompletionState::Pending};
    std::coroutine_handle<> handle;
    std::optional<T> result;
    std::optional<Error> error;
};

template <>
struct IOState<void> {
    std::atomic<CompletionState> state{CompletionState::Pending};
    std::coroutine_handle<> handle;
    std::optional<Error> error;
};

} // namespace detail

template <typename T>
class AsyncIOOperation {
public:
    virtual ~AsyncIOOperation() = default;

    // NOLINTNEXTLINE(readability-identifier-naming)
    bool await_ready() const noexcept {
        return state_->state.load(std::memory_order_acquire) != detail::CompletionState::Pending;
    }

    // NOLINTNEXTLINE(readability-identifier-naming)
    bool await_suspend(std::coroutine_handle<> handle) noexcept {
        state_->handle = handle;
        if (timeout_.count() > 0) {
            // 使用 weak_ptr 避免悬空访问
            auto weakState = std::weak_ptr<detail::IOState<T>>(state_);
            timerId_ = CoroutineScheduler::instance().scheduleCallback(timeout_, [weakState]() {
                auto state = weakState.lock();
                if (!state) {
                    return; // 对象已销毁
                }
                auto expected = detail::CompletionState::Pending;
                if (state->state.compare_exchange_strong(expected,
                                                         detail::CompletionState::TimedOut,
                                                         std::memory_order_acq_rel)) {
                    state->error = Error::ConnectionTimeout;
                    scheduleResume(*state);
                }
            });
        }
        startOperation();
        return state_->state.load(std::memory_order_acquire) == detail::CompletionState::Pending;
    }

    // NOLINTNEXTLINE(readability-identifier-naming)
    IOResult<T> await_resume() {
        auto s = state_->state.load(std::memory_order_acquire);
        if (s == detail::CompletionState::Failed || s == detail::CompletionState::TimedOut) {
            return std::unexpected(state_->error.value_or(Error::NetworkError));
        }
        if (s == detail::CompletionState::Completed && state_->result.has_value()) {
            return std::move(*state_->result);
        }
        return std::unexpected(Error::NetworkError);
    }

    void setTimeout(std::chrono::milliseconds timeout) noexcept {
        timeout_ = timeout;
    }

protected:
    AsyncIOOperation() : state_(std::make_shared<detail::IOState<T>>()) {}

    virtual void startOperation() noexcept = 0;

    void complete(T result) noexcept {
        auto expected = detail::CompletionState::Pending;
        if (state_->state.compare_exchange_strong(expected,
                                                  detail::CompletionState::Completed,
                                                  std::memory_order_acq_rel)) {
            state_->result.emplace(std::move(result));
            cancelTimer();
            scheduleResume(*state_);
        }
    }

    void fail(Error error) noexcept {
        auto expected = detail::CompletionState::Pending;
        if (state_->state.compare_exchange_strong(expected,
                                                  detail::CompletionState::Failed,
                                                  std::memory_order_acq_rel)) {
            state_->error = error;
            cancelTimer();
            scheduleResume(*state_);
        }
    }

    // 获取协程句柄（供子类在 startOperation 中使用）
    [[nodiscard]] std::coroutine_handle<> getHandle() const noexcept {
        return state_->handle;
    }

    int32_t fd_{-1};

private:
    void cancelTimer() noexcept {
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    static void scheduleResume(detail::IOState<T>& state) noexcept {
        if (state.handle) {
            CoroutineScheduler::instance().schedule(state.handle);
        }
    }

    std::shared_ptr<detail::IOState<T>> state_;
    std::chrono::milliseconds timeout_{0};
    TimerId timerId_;
};

template <>
class AsyncIOOperation<void> {
public:
    virtual ~AsyncIOOperation() = default;

    // NOLINTNEXTLINE(readability-identifier-naming)
    bool await_ready() const noexcept {
        return state_->state.load(std::memory_order_acquire) != detail::CompletionState::Pending;
    }

    // NOLINTNEXTLINE(readability-identifier-naming)
    bool await_suspend(std::coroutine_handle<> handle) noexcept {
        state_->handle = handle;
        if (timeout_.count() > 0) {
            // 使用 weak_ptr 避免悬空访问
            auto weakState = std::weak_ptr<detail::IOState<void>>(state_);
            timerId_ = CoroutineScheduler::instance().scheduleCallback(timeout_, [weakState]() {
                auto state = weakState.lock();
                if (!state) {
                    return; // 对象已销毁
                }
                auto expected = detail::CompletionState::Pending;
                if (state->state.compare_exchange_strong(expected,
                                                         detail::CompletionState::TimedOut,
                                                         std::memory_order_acq_rel)) {
                    state->error = Error::ConnectionTimeout;
                    scheduleResume(*state);
                }
            });
        }
        startOperation();
        return state_->state.load(std::memory_order_acquire) == detail::CompletionState::Pending;
    }

    // NOLINTNEXTLINE(readability-identifier-naming)
    IOResult<void> await_resume() {
        auto s = state_->state.load(std::memory_order_acquire);
        if (s == detail::CompletionState::Failed || s == detail::CompletionState::TimedOut) {
            return std::unexpected(state_->error.value_or(Error::NetworkError));
        }
        return {};
    }

    void setTimeout(std::chrono::milliseconds timeout) noexcept {
        timeout_ = timeout;
    }

protected:
    AsyncIOOperation() : state_(std::make_shared<detail::IOState<void>>()) {}

    virtual void startOperation() noexcept = 0;

    void complete() noexcept {
        auto expected = detail::CompletionState::Pending;
        if (state_->state.compare_exchange_strong(expected,
                                                  detail::CompletionState::Completed,
                                                  std::memory_order_acq_rel)) {
            cancelTimer();
            scheduleResume(*state_);
        }
    }

    void fail(Error error) noexcept {
        auto expected = detail::CompletionState::Pending;
        if (state_->state.compare_exchange_strong(expected,
                                                  detail::CompletionState::Failed,
                                                  std::memory_order_acq_rel)) {
            state_->error = error;
            cancelTimer();
            scheduleResume(*state_);
        }
    }

    // 获取协程句柄（供子类在 startOperation 中使用）
    [[nodiscard]] std::coroutine_handle<> getHandle() const noexcept {
        return state_->handle;
    }

    // 设置状态（供子类使用，如 AsyncConnect 异步完成时）
    void setState(detail::CompletionState state) noexcept {
        state_->state.store(state, std::memory_order_release);
    }

    // 设置错误（供子类使用）
    void setError(Error error) noexcept {
        state_->error = error;
    }

    // 获取当前状态
    [[nodiscard]] detail::CompletionState getState() const noexcept {
        return state_->state.load(std::memory_order_acquire);
    }

    // 获取错误值
    [[nodiscard]] std::optional<Error> getError() const noexcept {
        return state_->error;
    }

    int32_t fd_{-1};

private:
    void cancelTimer() noexcept {
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    static void scheduleResume(detail::IOState<void>& state) noexcept {
        if (state.handle) {
            CoroutineScheduler::instance().schedule(state.handle);
        }
    }

    std::shared_ptr<detail::IOState<void>> state_;
    std::chrono::milliseconds timeout_{0};
    TimerId timerId_;
};

} // namespace slark
