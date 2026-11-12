// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "CoroutineControl.hpp"
#include "CoroutineScheduler.hpp"
#include "Error.h"

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
        releaseTransferLeases(handle.promise());
        markExternalAwait(handle.promise());
        gate_ = makeResumeGate(handle);
        registerExternalGate(handle.promise(), gate_);
        auto weakGate = std::weak_ptr<ResumeGate>(gate_);
        timerId_ = CoroutineScheduler::instance().scheduleCallback(duration_, [weakGate]() {
            if (auto gate = weakGate.lock()) {
                gate->signal();
            }
        });
        cancelReg_ = bindGateCancellation(handle.promise(), gate_);
        return gate_->arm();
    }

    void await_resume() {
        if (gate_ && gate_->consumeCancelled()) {
            throw CancelledException("Timer cancelled");
        }
    }

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
    CancellationTokenRegistration cancelReg_;
};

template <typename T>
    requires(std::is_void_v<T> || std::copy_constructible<T>)
class CompletionAwaiter {
    using ResultType = std::conditional_t<std::is_void_v<T>, std::monostate, T>;
    using WaiterList = std::list<std::shared_ptr<ResumeGate>>;

    struct State {
        std::mutex mutex;
        bool completed = false;
        std::optional<ResultType> result;
        std::exception_ptr exception;
        WaiterList waiters;
    };

 public:
    class Waiter {
     public:
        explicit Waiter(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

        Waiter(const Waiter&) = delete;
        Waiter& operator=(const Waiter&) = delete;

        ~Waiter() {
            if (linked_) {
                std::lock_guard lock(state_->mutex);
                state_->waiters.erase(position_);
            }
            if (gate_) {
                gate_->disarm();
            }
        }

        bool await_ready() const noexcept {
            std::lock_guard lock(state_->mutex);
            return state_->completed;
        }

        template <typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            releaseTransferLeases(handle.promise());
            markExternalAwait(handle.promise());
            gate_ = makeResumeGate(handle);
            std::lock_guard lock(state_->mutex);
            if (state_->completed) {
                gate_->disarm();
                return false;
            }
            state_->waiters.push_back(gate_);
            position_ = std::prev(state_->waiters.end());
            linked_ = true;
            registerExternalGate(handle.promise(), gate_);
            cancelReg_ = bindGateCancellation(handle.promise(), gate_);
            return gate_->arm();
        }

        T await_resume() {
            if (gate_ && gate_->consumeCancelled()) {
                throw CancelledException("Completion cancelled");
            }
            std::lock_guard lock(state_->mutex);
            if (state_->exception) {
                std::rethrow_exception(state_->exception);
            }
            if constexpr (std::is_void_v<T>) {
                return;
            } else {
                return *state_->result;
            }
        }

     private:
        std::shared_ptr<State> state_;
        std::shared_ptr<ResumeGate> gate_;
        WaiterList::iterator position_;
        bool linked_ = false;
        CancellationTokenRegistration cancelReg_;
    };

    CompletionAwaiter() : state_(std::make_shared<State>()) {}

    CompletionAwaiter(const CompletionAwaiter&) = delete;
    CompletionAwaiter& operator=(const CompletionAwaiter&) = delete;

    ~CompletionAwaiter() {
        std::vector<std::shared_ptr<ResumeGate>> pending;
        {
            std::lock_guard lock(state_->mutex);
            pending.assign(state_->waiters.begin(), state_->waiters.end());
        }
        for (const auto& gate : pending) {
            gate->disarm();
        }
    }

    [[nodiscard]] bool isCompleted() const noexcept {
        std::lock_guard lock(state_->mutex);
        return state_->completed;
    }

    [[nodiscard]] Waiter operator co_await() noexcept {
        return Waiter(state_);
    }

    template <typename U>
    void complete(U value) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(!std::is_void_v<T> && std::is_convertible_v<U, T>)
    {
        finish([&] {
            state_->result.emplace(T(std::move(value)));
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
            state_->exception = std::move(exception);
        });
    }

 private:
    template <typename Publish>
    void finish(Publish&& publish) noexcept {
        std::vector<std::shared_ptr<ResumeGate>> toSignal;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->completed) {
                return;
            }
            publish();
            state_->completed = true;
            toSignal.assign(state_->waiters.begin(), state_->waiters.end());
        }
        for (const auto& gate : toSignal) {
            gate->signal();
        }
    }

    std::shared_ptr<State> state_;
};

}  // namespace slark
