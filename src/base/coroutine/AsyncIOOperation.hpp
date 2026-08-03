// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <coroutine>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include "CancellationToken.h"
#include "CoroutineControl.hpp"
#include "CoroutineScheduler.hpp"
#include "Error.h"
#include "Timer.h"

namespace slark {

template <typename T>
using IOResult = std::expected<T, Error>;

enum class IOOpState : uint8_t {
    Pending = 0,
    Starting,
    Waiting,
    Completed,
    Failed,
    TimedOut,
    Cancelled,
};

struct SharedCompletion {
    std::atomic<IOOpState> state{IOOpState::Pending};
    mutable std::mutex mutex;
    std::shared_ptr<ResumeGate> gate;
    std::function<void()> cleanup;
    std::optional<Error> error;

    template <typename Publish>
    bool tryComplete(IOOpState newState,
                     std::optional<Error> newError,
                     Publish&& publish) noexcept {
        std::shared_ptr<ResumeGate> resumeGate;
        std::function<void()> cleanupTask;
        IOOpState previous;
        {
            std::lock_guard lock(mutex);
            previous = state.load(std::memory_order_relaxed);
            if (previous == IOOpState::Completed || previous == IOOpState::Failed ||
                previous == IOOpState::TimedOut || previous == IOOpState::Cancelled) {
                return false;
            }
            publish();
            error = newError;
            state.store(newState, std::memory_order_release);
            resumeGate = gate;
            cleanupTask = std::exchange(cleanup, nullptr);
        }

        if ((previous == IOOpState::Starting || previous == IOOpState::Waiting) && resumeGate) {
            resumeGate->signal();
        }
        if (cleanupTask) {
            cleanupTask();
        }
        return true;
    }

    bool begin(std::shared_ptr<ResumeGate> newGate) noexcept {
        std::lock_guard lock(mutex);
        gate = std::move(newGate);
        auto expected = IOOpState::Pending;
        return state.compare_exchange_strong(expected, IOOpState::Starting,
                                             std::memory_order_acq_rel);
    }

    bool wait() noexcept {
        std::lock_guard lock(mutex);
        auto expected = IOOpState::Starting;
        return state.compare_exchange_strong(expected, IOOpState::Waiting,
                                             std::memory_order_acq_rel);
    }

    void setCleanup(std::function<void()> task) noexcept {
        std::lock_guard lock(mutex);
        cleanup = std::move(task);
    }

    [[nodiscard]] std::shared_ptr<ResumeGate> getGate() const noexcept {
        std::lock_guard lock(mutex);
        return gate;
    }

    [[nodiscard]] std::optional<Error> getError() const noexcept {
        std::lock_guard lock(mutex);
        return error;
    }

    void cancel() noexcept {
        std::shared_ptr<ResumeGate> resumeGate;
        std::function<void()> cleanupTask;
        {
            std::lock_guard lock(mutex);
            auto current = state.load(std::memory_order_relaxed);
            if (current == IOOpState::Completed || current == IOOpState::Failed ||
                current == IOOpState::TimedOut || current == IOOpState::Cancelled) {
                return;
            }
            state.store(IOOpState::Cancelled, std::memory_order_release);
            resumeGate = gate;
            cleanupTask = std::exchange(cleanup, nullptr);
        }
        if (resumeGate) {
            resumeGate->disarm();
        }
        if (cleanupTask) {
            cleanupTask();
        }
    }

    // 外部取消（C-06）：设置 Cancelled 并唤醒挂起的协程，cleanup 照常执行
    void requestCancel() noexcept {
        std::shared_ptr<ResumeGate> resumeGate;
        std::function<void()> cleanupTask;
        {
            std::lock_guard lock(mutex);
            auto current = state.load(std::memory_order_relaxed);
            if (current == IOOpState::Completed || current == IOOpState::Failed ||
                current == IOOpState::TimedOut || current == IOOpState::Cancelled) {
                return;
            }
            state.store(IOOpState::Cancelled, std::memory_order_release);
            error = Error::Cancelled;
            resumeGate = gate;
            cleanupTask = std::exchange(cleanup, nullptr);
        }
        if (resumeGate) {
            resumeGate->signal();
        }
        if (cleanupTask) {
            cleanupTask();
        }
    }
};

template <typename Derived, typename T>
class AsyncIOOperationBase {
 public:
    AsyncIOOperationBase() : completion_(std::make_shared<SharedCompletion>()) {}

    virtual ~AsyncIOOperationBase() {
        cancelTimer();
        completion_->cancel();
    }

    bool await_ready() const noexcept {
        return isTerminal(completion_->state.load(std::memory_order_acquire));
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto gate = makeResumeGate(handle);
        if (!completion_->begin(gate)) {
            gate->disarm();
            return false;
        }

        scheduleTimeout();
        startOperation();
        if (completion_->wait()) {
            return gate->arm();
        }
        gate->disarm();
        return false;
    }

    IOResult<T> await_resume() {
        auto s = completion_->state.load(std::memory_order_acquire);
        if (s == IOOpState::Failed || s == IOOpState::TimedOut || s == IOOpState::Cancelled) {
            return std::unexpected(completion_->getError().value_or(Error::NetworkError));
        }
        return derived().getResult();
    }

    void setTimeout(std::chrono::milliseconds t) noexcept {
        timeout_ = t;
    }

    // 外部取消（C-06）：token 取消时唤醒挂起的协程
    void setCancellationToken(CancellationToken token) {
        if (token.isCancelled()) {
            completion_->requestCancel();
            return;
        }
        auto weakCompletion = std::weak_ptr<SharedCompletion>(completion_);
        cancelReg_ = token.registerCallback([weakCompletion]() {
            if (auto completion = weakCompletion.lock()) {
                completion->requestCancel();
            }
        });
    }

 protected:
    int32_t fd_{-1};

    virtual void startOperation() noexcept = 0;

    void complete(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        completion_->tryComplete(IOOpState::Completed, std::nullopt, [this, value = std::move(value)]() mutable {
            derived().storeResult(std::move(value));
        });
    }

    void fail(Error error) noexcept {
        completion_->tryComplete(IOOpState::Failed, error, [] {});
    }

    void setCleanup(std::function<void()> cleanup) noexcept {
        completion_->setCleanup(std::move(cleanup));
    }

    [[nodiscard]] std::shared_ptr<ResumeGate> ioGate() const noexcept {
        return completion_->getGate();
    }

 private:
    CancellationTokenRegistration cancelReg_;
    [[nodiscard]] static bool isTerminal(IOOpState state) noexcept {
        return state == IOOpState::Completed || state == IOOpState::Failed ||
               state == IOOpState::TimedOut || state == IOOpState::Cancelled;
    }

    Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    void scheduleTimeout() noexcept {
        if (timeout_.count() <= 0) {
            return;
        }
        auto weakCompletion = std::weak_ptr<SharedCompletion>(completion_);
        timerId_ = CoroutineScheduler::instance().scheduleCallback(timeout_, [weakCompletion]() {
            if (auto completion = weakCompletion.lock()) {
                completion->tryComplete(IOOpState::TimedOut, Error::ConnectionTimeout, [] {});
            }
        });
    }

    void cancelTimer() noexcept {
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    std::shared_ptr<SharedCompletion> completion_;
    std::chrono::milliseconds timeout_{0};
    TimerId timerId_;
};

template <typename Derived>
class AsyncIOOperationBase<Derived, void> {
 public:
    AsyncIOOperationBase() : completion_(std::make_shared<SharedCompletion>()) {}

    virtual ~AsyncIOOperationBase() {
        cancelTimer();
        completion_->cancel();
    }

    bool await_ready() const noexcept {
        return isTerminal(completion_->state.load(std::memory_order_acquire));
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto gate = makeResumeGate(handle);
        if (!completion_->begin(gate)) {
            gate->disarm();
            return false;
        }

        scheduleTimeout();
        startOperation();
        if (completion_->wait()) {
            return gate->arm();
        }
        gate->disarm();
        return false;
    }

    IOResult<void> await_resume() {
        auto s = completion_->state.load(std::memory_order_acquire);
        if (s == IOOpState::Failed || s == IOOpState::TimedOut || s == IOOpState::Cancelled) {
            return std::unexpected(completion_->getError().value_or(Error::NetworkError));
        }
        return {};
    }

    void setTimeout(std::chrono::milliseconds t) noexcept {
        timeout_ = t;
    }

    // 外部取消（C-06）：token 取消时唤醒挂起的协程
    void setCancellationToken(CancellationToken token) {
        if (token.isCancelled()) {
            completion_->requestCancel();
            return;
        }
        auto weakCompletion = std::weak_ptr<SharedCompletion>(completion_);
        cancelReg_ = token.registerCallback([weakCompletion]() {
            if (auto completion = weakCompletion.lock()) {
                completion->requestCancel();
            }
        });
    }

 protected:
    int32_t fd_{-1};

    virtual void startOperation() noexcept = 0;

    void complete() noexcept {
        completion_->tryComplete(IOOpState::Completed, std::nullopt, [] {});
    }

    void fail(Error error) noexcept {
        completion_->tryComplete(IOOpState::Failed, error, [] {});
    }

    [[nodiscard]] IOOpState getState() const noexcept {
        return completion_->state.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<Error> getError() const noexcept {
        return completion_->getError();
    }

    void setCleanup(std::function<void()> cleanup) noexcept {
        completion_->setCleanup(std::move(cleanup));
    }

    [[nodiscard]] std::shared_ptr<ResumeGate> ioGate() const noexcept {
        return completion_->getGate();
    }

 private:
    CancellationTokenRegistration cancelReg_;
    [[nodiscard]] static bool isTerminal(IOOpState state) noexcept {
        return state == IOOpState::Completed || state == IOOpState::Failed ||
               state == IOOpState::TimedOut || state == IOOpState::Cancelled;
    }

    Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    void scheduleTimeout() noexcept {
        if (timeout_.count() <= 0) {
            return;
        }
        auto weakCompletion = std::weak_ptr<SharedCompletion>(completion_);
        timerId_ = CoroutineScheduler::instance().scheduleCallback(timeout_, [weakCompletion]() {
            if (auto completion = weakCompletion.lock()) {
                completion->tryComplete(IOOpState::TimedOut, Error::ConnectionTimeout, [] {});
            }
        });
    }

    void cancelTimer() noexcept {
        if (timerId_.isValid()) {
            CoroutineScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    std::shared_ptr<SharedCompletion> completion_;
    std::chrono::milliseconds timeout_{0};
    TimerId timerId_;
};

}  // namespace slark
