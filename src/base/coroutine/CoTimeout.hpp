// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "CancellationToken.h"
#include "CoroutineScheduler.hpp"
#include "Error.h"
#include "ScopeGuard.h"
#include "Task.hpp"

namespace slark::coro {

namespace detail {

struct TimeoutUnit {};

template <typename T>
using TimeoutValue = std::conditional_t<std::is_void_v<T>, TimeoutUnit, T>;

template <typename T>
struct TimeoutState {
    enum Decision : int { Undecided, ChildDone, TimedOut, Cancelled };

    bool decide(Decision next) noexcept {
        int expected = Undecided;
        return decision.compare_exchange_strong(expected, next, std::memory_order_acq_rel,
                                                std::memory_order_acquire);
    }

    std::atomic<int> decision{Undecided};
    CancellationToken expired;
    std::optional<CancellationToken> parent;
    std::optional<TimeoutValue<T>> value;
    std::exception_ptr error;
};

template <typename T>
Task<void> runTimedChild(std::shared_ptr<TimeoutState<T>> state,
                         std::optional<CancellationToken> parent, Task<T> task) {
    try {
        if constexpr (std::is_void_v<T>) {
            co_await std::move(task);
            state->value.emplace();
        } else {
            state->value.emplace(co_await std::move(task));
        }
    } catch (const CancelledException&) {
        state->decide(TimeoutState<T>::Cancelled);
        co_return;
    } catch (...) {
        state->error = std::current_exception();
    }
    const bool cancelled = parent && parent->isCancelled();
    state->decide(cancelled ? TimeoutState<T>::Cancelled : TimeoutState<T>::ChildDone);
}

template <typename T>
Task<T> withTimeoutImpl(Task<T> task, std::chrono::milliseconds timeout) {
    ::slark::detail::requireValidTask(task);
    auto parent = co_await cancellation_token();
    if (parent && parent->isCancelled()) {
        throw CancelledException("Operation cancelled");
    }

    auto state = std::make_shared<TimeoutState<T>>();
    state->parent = parent;
    // A cancelled timer keeps its captures until it expires, so it must not own the state.
    auto timerId = CoroutineScheduler::instance().scheduleCallback(
        timeout, [weak = std::weak_ptr<TimeoutState<T>>(state)] {
            auto locked = weak.lock();
            if (!locked) {
                return;
            }
            if (locked->parent && locked->parent->isCancelled()) {
                locked->decide(TimeoutState<T>::Cancelled);
            } else if (locked->decide(TimeoutState<T>::TimedOut)) {
                locked->expired.cancel();
            }
        });
    ScopeGuard timerGuard([timerId] { CoroutineScheduler::instance().cancel(timerId); });

    Task<void> child = runTimedChild<T>(state, parent, std::move(task));
    attach(child.getHandle().promise(), SpawnEdge{}, &state->expired);
    co_await std::move(child);

    const int decision = state->decision.load(std::memory_order_acquire);
    if (decision == TimeoutState<T>::TimedOut) {
        throw TimeoutException("Operation timed out");
    }
    if (decision == TimeoutState<T>::Cancelled) {
        throw CancelledException("Operation cancelled");
    }
    if (decision != TimeoutState<T>::ChildDone) {
        throw std::logic_error("with_timeout joined without a decision");
    }
    if (state->error) {
        std::rethrow_exception(state->error);
    }
    if constexpr (std::is_void_v<T>) {
        co_return;
    } else {
        co_return std::move(*state->value);
    }
}

}  // namespace detail

template <typename T>
Task<T> with_timeout(Task<T> task, std::chrono::milliseconds timeout) {
    return detail::withTimeoutImpl(std::move(task), timeout);
}

}  // namespace slark::coro
