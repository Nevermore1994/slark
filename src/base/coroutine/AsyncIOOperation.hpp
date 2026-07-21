//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Coroutine.hpp"
#include "CoroutineScheduler.hpp"
#include "Error.h"
#include "SharedCoroutineHandle.hpp"
#include "Timer.h"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

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

enum class ResumePhase : uint8_t {
    Registering = 0,
    Suspended,
    Resumed,
};

template <typename T>
struct IOState {
    std::atomic<CompletionState> state{CompletionState::Pending};
    std::atomic<ResumePhase> resumePhase{ResumePhase::Registering};
    std::shared_ptr<SharedCoroutineHandle> resumeHandle;
    std::optional<T> result;
    std::optional<Error> error;
    std::function<void()> cleanup;
};

template <>
struct IOState<void> {
    std::atomic<CompletionState> state{CompletionState::Pending};
    std::atomic<ResumePhase> resumePhase{ResumePhase::Registering};
    std::shared_ptr<SharedCoroutineHandle> resumeHandle;
    std::optional<Error> error;
    std::function<void()> cleanup;
};

} // namespace detail

template <typename T>
class AsyncIOOperation {
public:
    virtual ~AsyncIOOperation() = default;

    bool await_ready() const noexcept {
        return state_->state.load(std::memory_order_acquire) != detail::CompletionState::Pending;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        state_->resumeHandle = handle.promise().makeResumeHandle();
        scheduleTimeout();
        startOperation();
        if (state_->state.load(std::memory_order_acquire) != detail::CompletionState::Pending) {
            return false;
        }
        state_->resumePhase.store(detail::ResumePhase::Suspended, std::memory_order_release);
        if (state_->state.load(std::memory_order_acquire) != detail::CompletionState::Pending) {
            auto expected = detail::ResumePhase::Suspended;
            return !state_->resumePhase.compare_exchange_strong(expected, detail::ResumePhase::Resumed,
                                                                 std::memory_order_acq_rel);
        }
        return state_->resumeHandle && state_->resumeHandle->suspend();
    }

    IOResult<T> await_resume() {
        auto state = state_->state.load(std::memory_order_acquire);
        if (state == detail::CompletionState::Failed || state == detail::CompletionState::TimedOut) {
            return std::unexpected(state_->error.value_or(Error::NetworkError));
        }
        if (state == detail::CompletionState::Completed && state_->result.has_value()) {
            return std::move(*state_->result);
        }
        return std::unexpected(Error::NetworkError);
    }

    void setTimeout(std::chrono::milliseconds timeout) noexcept { timeout_ = timeout; }

protected:
    AsyncIOOperation() : state_(std::make_shared<detail::IOState<T>>()) {}

    virtual void startOperation() noexcept = 0;

    void setCleanup(std::function<void()> cleanup) noexcept { state_->cleanup = std::move(cleanup); }

    void complete(T result) noexcept {
        auto expected = detail::CompletionState::Pending;
        if (state_->state.compare_exchange_strong(expected, detail::CompletionState::Completed,
                                                  std::memory_order_acq_rel)) {
            state_->result.emplace(std::move(result));
            cancelTimer();
            finishState(*state_);
        }
    }

    void fail(Error error) noexcept {
        auto expected = detail::CompletionState::Pending;
        if (state_->state.compare_exchange_strong(expected, detail::CompletionState::Failed,
                                                  std::memory_order_acq_rel)) {
            state_->error = error;
            cancelTimer();
            finishState(*state_);
        }
    }

    [[nodiscard]] std::shared_ptr<SharedCoroutineHandle> getSharedHandle() const noexcept {
        return state_->resumeHandle;
    }

    int32_t fd_{-1};

private:
    void scheduleTimeout() noexcept {
        if (timeout_.count() <= 0) {
            return;
        }
        auto weakState = std::weak_ptr<detail::IOState<T>>(state_);
        timerId_ = CoroutineScheduler::instance().scheduleCallback(timeout_, [weakState]() {
            auto state = weakState.lock();
            if (!state) {
                return;
            }
            auto expected = detail::CompletionState::Pending;
            if (state->state.compare_exchange_strong(expected, detail::CompletionState::TimedOut,
                                                     std::memory_order_acq_rel)) {
                state->error = Error::ConnectionTimeout;
                finishState(*state);
            }
        });
    }

    void cancelTimer() noexcept {
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    static void finishState(detail::IOState<T>& state) noexcept {
        if (auto cleanup = std::exchange(state.cleanup, nullptr)) {
            cleanup();
        }
        scheduleResume(state);
    }

    static void scheduleResume(detail::IOState<T>& state) noexcept {
        auto expected = detail::ResumePhase::Suspended;
        if (state.resumePhase.compare_exchange_strong(expected, detail::ResumePhase::Resumed,
                                                      std::memory_order_acq_rel)) {
            CoroutineScheduler::instance().schedule(state.resumeHandle);
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

    bool await_ready() const noexcept {
        return state_->state.load(std::memory_order_acquire) != detail::CompletionState::Pending;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        state_->resumeHandle = handle.promise().makeResumeHandle();
        scheduleTimeout();
        startOperation();
        if (state_->state.load(std::memory_order_acquire) != detail::CompletionState::Pending) {
            return false;
        }
        state_->resumePhase.store(detail::ResumePhase::Suspended, std::memory_order_release);
        if (state_->state.load(std::memory_order_acquire) != detail::CompletionState::Pending) {
            auto expected = detail::ResumePhase::Suspended;
            return !state_->resumePhase.compare_exchange_strong(expected, detail::ResumePhase::Resumed,
                                                                 std::memory_order_acq_rel);
        }
        return state_->resumeHandle && state_->resumeHandle->suspend();
    }

    IOResult<void> await_resume() {
        auto state = state_->state.load(std::memory_order_acquire);
        if (state == detail::CompletionState::Failed || state == detail::CompletionState::TimedOut) {
            return std::unexpected(state_->error.value_or(Error::NetworkError));
        }
        return {};
    }

    void setTimeout(std::chrono::milliseconds timeout) noexcept { timeout_ = timeout; }

protected:
    AsyncIOOperation() : state_(std::make_shared<detail::IOState<void>>()) {}

    virtual void startOperation() noexcept = 0;

    void setCleanup(std::function<void()> cleanup) noexcept { state_->cleanup = std::move(cleanup); }

    void complete() noexcept {
        auto expected = detail::CompletionState::Pending;
        if (state_->state.compare_exchange_strong(expected, detail::CompletionState::Completed,
                                                  std::memory_order_acq_rel)) {
            cancelTimer();
            finishState(*state_);
        }
    }

    void fail(Error error) noexcept {
        auto expected = detail::CompletionState::Pending;
        if (state_->state.compare_exchange_strong(expected, detail::CompletionState::Failed,
                                                  std::memory_order_acq_rel)) {
            state_->error = error;
            cancelTimer();
            finishState(*state_);
        }
    }

    [[nodiscard]] std::shared_ptr<SharedCoroutineHandle> getSharedHandle() const noexcept {
        return state_->resumeHandle;
    }

    [[nodiscard]] detail::CompletionState getState() const noexcept {
        return state_->state.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<Error> getError() const noexcept { return state_->error; }

    int32_t fd_{-1};

private:
    void scheduleTimeout() noexcept {
        if (timeout_.count() <= 0) {
            return;
        }
        auto weakState = std::weak_ptr<detail::IOState<void>>(state_);
        timerId_ = CoroutineScheduler::instance().scheduleCallback(timeout_, [weakState]() {
            auto state = weakState.lock();
            if (!state) {
                return;
            }
            auto expected = detail::CompletionState::Pending;
            if (state->state.compare_exchange_strong(expected, detail::CompletionState::TimedOut,
                                                     std::memory_order_acq_rel)) {
                state->error = Error::ConnectionTimeout;
                finishState(*state);
            }
        });
    }

    void cancelTimer() noexcept {
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    static void finishState(detail::IOState<void>& state) noexcept {
        if (auto cleanup = std::exchange(state.cleanup, nullptr)) {
            cleanup();
        }
        scheduleResume(state);
    }

    static void scheduleResume(detail::IOState<void>& state) noexcept {
        auto expected = detail::ResumePhase::Suspended;
        if (state.resumePhase.compare_exchange_strong(expected, detail::ResumePhase::Resumed,
                                                      std::memory_order_acq_rel)) {
            CoroutineScheduler::instance().schedule(state.resumeHandle);
        }
    }

    std::shared_ptr<detail::IOState<void>> state_;
    std::chrono::milliseconds timeout_{0};
    TimerId timerId_;
};

} // namespace slark
