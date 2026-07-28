// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "CoroutineScheduler.hpp"
#include "Error.h"
#include "Timer.h"

namespace slark {

template <typename T>
using IOResult = std::expected<T, Error>;

enum class IOOpState : uint8_t {
    Pending = 0,
    Completed,
    Failed,
    TimedOut,
};

struct SharedCompletion {
    std::atomic<IOOpState> state{IOOpState::Pending};
    std::shared_ptr<std::atomic<bool>> aliveFlag;
    std::coroutine_handle<> handle;
    std::function<void()> cleanup;
    std::optional<Error> error;

    bool tryComplete(IOOpState newState) noexcept {
        auto expected = IOOpState::Pending;
        if (state.compare_exchange_strong(expected, newState, std::memory_order_acq_rel)) {
            if (cleanup) {
                cleanup();
            }
            if (aliveFlag && aliveFlag->load(std::memory_order_acquire)) {
                handle.resume();
            }
            return true;
        }
        return false;
    }
};

template <typename Derived, typename T>
class AsyncIOOperationBase {
 public:
    AsyncIOOperationBase() : completion_(std::make_shared<SharedCompletion>()) {}

    virtual ~AsyncIOOperationBase() {
        if (aliveFlag_) {
            *aliveFlag_ = false;
            aliveFlag_.reset();
        }
        cancelTimer();
        if (auto cleanup = std::exchange(cleanup_, nullptr)) {
            cleanup();
        }
    }

    bool await_ready() const noexcept {
        return completion_->state.load(std::memory_order_acquire) != IOOpState::Pending;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        aliveFlag_ = std::make_shared<std::atomic<bool>>(true);
        completion_->aliveFlag = aliveFlag_;
        completion_->handle = handle_;
        completion_->cleanup = {};
        scheduleTimeout();
        startOperation();
        return completion_->state.load(std::memory_order_acquire) == IOOpState::Pending;
    }

    IOResult<T> await_resume() {
        auto s = completion_->state.load(std::memory_order_acquire);
        if (s == IOOpState::Failed || s == IOOpState::TimedOut) {
            return std::unexpected(error_.value_or(Error::NetworkError));
        }
        return derived().getResult();
    }

    void setTimeout(std::chrono::milliseconds t) noexcept {
        timeout_ = t;
    }

 protected:
    int32_t fd_{-1};
    std::optional<Error> error_;
    std::coroutine_handle<> handle_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_;

    virtual void startOperation() noexcept = 0;

    void complete(T value) noexcept {
        derived().storeResult(std::move(value));
        finish(IOOpState::Completed);
    }

    void fail(Error error) noexcept {
        error_ = error;
        finish(IOOpState::Failed);
    }

    void setCleanup(std::function<void()> cleanup) noexcept {
        cleanup_ = std::move(cleanup);
        completion_->cleanup = cleanup_;
    }

    [[nodiscard]] std::coroutine_handle<> ioHandle() const noexcept {
        return handle_;
    }

    [[nodiscard]] std::shared_ptr<std::atomic<bool>> ioAliveFlag() const noexcept {
        return aliveFlag_;
    }

 private:
    Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    void finish(IOOpState newState) noexcept {
        completion_->cleanup = nullptr;
        if (auto cleanup = std::exchange(cleanup_, nullptr)) {
            cleanup();
        }
        completion_->error = error_;
        completion_->tryComplete(newState);
    }

    void scheduleTimeout() noexcept {
        if (timeout_.count() <= 0) {
            return;
        }
        auto weakCompletion = std::weak_ptr<SharedCompletion>(completion_);
        timerId_ = CoroutineScheduler::instance().scheduleCallback(timeout_, [weakCompletion]() {
            auto c = weakCompletion.lock();
            if (!c) {
                return;
            }
            if (!c->aliveFlag || !c->aliveFlag->load(std::memory_order_acquire)) {
                return;
            }
            c->error = Error::ConnectionTimeout;
            c->tryComplete(IOOpState::TimedOut);
        });
    }

    void cancelTimer() noexcept {
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    std::shared_ptr<SharedCompletion> completion_;
    std::function<void()> cleanup_;
    std::chrono::milliseconds timeout_{0};
    TimerId timerId_;
};

template <typename Derived>
class AsyncIOOperationBase<Derived, void> {
 public:
    AsyncIOOperationBase() : completion_(std::make_shared<SharedCompletion>()) {}

    virtual ~AsyncIOOperationBase() {
        if (aliveFlag_) {
            *aliveFlag_ = false;
            aliveFlag_.reset();
        }
        cancelTimer();
        if (auto cleanup = std::exchange(cleanup_, nullptr)) {
            cleanup();
        }
    }

    bool await_ready() const noexcept {
        return completion_->state.load(std::memory_order_acquire) != IOOpState::Pending;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        aliveFlag_ = std::make_shared<std::atomic<bool>>(true);
        completion_->aliveFlag = aliveFlag_;
        completion_->handle = handle_;
        completion_->cleanup = {};
        scheduleTimeout();
        startOperation();
        return completion_->state.load(std::memory_order_acquire) == IOOpState::Pending;
    }

    IOResult<void> await_resume() {
        auto s = completion_->state.load(std::memory_order_acquire);
        if (s == IOOpState::Failed || s == IOOpState::TimedOut) {
            return std::unexpected(error_.value_or(Error::NetworkError));
        }
        return {};
    }

    void setTimeout(std::chrono::milliseconds t) noexcept {
        timeout_ = t;
    }

 protected:
    int32_t fd_{-1};
    std::optional<Error> error_;
    std::coroutine_handle<> handle_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_;

    virtual void startOperation() noexcept = 0;

    void complete() noexcept {
        finish(IOOpState::Completed);
    }

    void fail(Error error) noexcept {
        error_ = error;
        finish(IOOpState::Failed);
    }

    [[nodiscard]] IOOpState getState() const noexcept {
        return completion_->state.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<Error> getError() const noexcept {
        return error_;
    }

    void setCleanup(std::function<void()> cleanup) noexcept {
        cleanup_ = std::move(cleanup);
        completion_->cleanup = cleanup_;
    }

    [[nodiscard]] std::coroutine_handle<> ioHandle() const noexcept {
        return handle_;
    }

    [[nodiscard]] std::shared_ptr<std::atomic<bool>> ioAliveFlag() const noexcept {
        return aliveFlag_;
    }

 private:
    Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    void finish(IOOpState newState) noexcept {
        completion_->cleanup = nullptr;
        if (auto cleanup = std::exchange(cleanup_, nullptr)) {
            cleanup();
        }
        completion_->error = error_;
        completion_->tryComplete(newState);
    }

    void scheduleTimeout() noexcept {
        if (timeout_.count() <= 0) {
            return;
        }
        auto weakCompletion = std::weak_ptr<SharedCompletion>(completion_);
        timerId_ = CoroutineScheduler::instance().scheduleCallback(timeout_, [weakCompletion]() {
            auto c = weakCompletion.lock();
            if (!c) {
                return;
            }
            if (!c->aliveFlag || !c->aliveFlag->load(std::memory_order_acquire)) {
                return;
            }
            c->error = Error::ConnectionTimeout;
            c->tryComplete(IOOpState::TimedOut);
        });
    }

    void cancelTimer() noexcept {
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    std::shared_ptr<SharedCompletion> completion_;
    std::function<void()> cleanup_;
    std::chrono::milliseconds timeout_{0};
    TimerId timerId_;
};

}  // namespace slark
