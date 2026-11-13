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
#include "ExternalWaitBase.hpp"

namespace slark {

class TimerAwaiter final : public ExternalWaitBase<TimerAwaiter, ScheduledCallback> {
public:
    explicit TimerAwaiter(std::chrono::milliseconds duration) noexcept : duration_(duration) {}

    [[nodiscard]] bool ready() const noexcept {
        return duration_.count() <= 0;
    }

    [[nodiscard]] std::optional<ScheduledCallback> start(const std::shared_ptr<ResumeGate>& gate) {
        return scheduleGateSignal(duration_, gate);
    }

    void finish() const noexcept {}

    static const char* cancelMessage() noexcept {
        return "Timer cancelled";
    }

private:
    std::chrono::milliseconds duration_;
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
    // Holds the waiter's place in the completion state; destroying it removes the waiter.
    struct Enlistment final {
        Enlistment(std::shared_ptr<State> owner, WaiterList::iterator where) noexcept
            : state(std::move(owner)), position(where) {}

        Enlistment(Enlistment&& other) noexcept
            : state(std::move(other.state)), position(other.position) {}

        Enlistment& operator=(Enlistment&& other) noexcept {
            if (this != &other) {
                unlink();
                state = std::move(other.state);
                position = other.position;
            }
            return *this;
        }

        Enlistment(const Enlistment&) = delete;
        Enlistment& operator=(const Enlistment&) = delete;

        ~Enlistment() {
            unlink();
        }

        void unlink() noexcept {
            if (auto owner = std::move(state)) {
                std::lock_guard lock(owner->mutex);
                owner->waiters.erase(position);
            }
        }

        std::shared_ptr<State> state;
        WaiterList::iterator position;
    };

    class Waiter final : public ExternalWaitBase<Waiter, Enlistment, T> {
     public:
        explicit Waiter(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

        [[nodiscard]] bool ready() const noexcept {
            std::lock_guard lock(state_->mutex);
            return state_->completed;
        }

        [[nodiscard]] std::optional<Enlistment> start(const std::shared_ptr<ResumeGate>& gate) {
            std::lock_guard lock(state_->mutex);
            if (state_->completed) {
                return std::nullopt;
            }
            state_->waiters.push_back(gate);
            return Enlistment(state_, std::prev(state_->waiters.end()));
        }

        T finish() {
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

        static const char* cancelMessage() noexcept {
            return "Completion cancelled";
        }

     private:
        std::shared_ptr<State> state_;
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
