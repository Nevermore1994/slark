// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <concepts>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "CoScope.hpp"
#include "CompositeWaitBase.hpp"
#include "Executor.h"
#include "Task.hpp"

namespace slark::coro {

struct VoidResult {};

template <typename T>
using WhenAllResultType = std::conditional_t<std::is_void_v<T>, VoidResult, T>;

namespace detail {

struct WhenContinuationState {
    std::mutex mutex;
    std::exception_ptr exception;
    std::shared_ptr<ResumeGate> continuationGate;

    void setContinuationGate(std::shared_ptr<ResumeGate> gate) noexcept {
        std::lock_guard lock(mutex);
        continuationGate = std::move(gate);
    }

    std::shared_ptr<ResumeGate> takeContinuationGate() noexcept {
        std::lock_guard lock(mutex);
        return std::exchange(continuationGate, nullptr);
    }

    void recordException(std::exception_ptr error) noexcept {
        std::lock_guard lock(mutex);
        if (!exception || (isCancellation(exception) && !isCancellation(error))) {
            exception = std::move(error);
        }
    }
};

}  // namespace detail

template <typename... Ts>
struct WhenAllState : detail::WhenContinuationState {
    std::tuple<std::optional<WhenAllResultType<Ts>>...> results;
    std::atomic<int> remaining{0};

    void childRefused() noexcept {
        recordException(
            std::make_exception_ptr(CancelledException("executor refused the child task")));
        finishChild();
    }

    void finishChild() noexcept {
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            if (auto gate = takeContinuationGate()) {
                gate->signal();
            }
        }
    }
};

namespace detail {

template <size_t I, typename... Ts>
Task<void> whenAllChild(std::shared_ptr<WhenAllState<Ts...>> state,
                        Task<std::tuple_element_t<I, std::tuple<Ts...>>> task,
                        CancellationToken group) {
    co_await RequireAttachedEdge{};
    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
    try {
        if constexpr (!std::is_void_v<T>) {
            auto value = co_await std::move(task);
            std::get<I>(state->results).emplace(std::move(value));
        } else {
            co_await std::move(task);
            std::get<I>(state->results).emplace(VoidResult{});
        }
    } catch (...) {
        auto error = std::current_exception();
        if (!isCancellation(error)) {
            group.cancel();
        }
        state->recordException(std::move(error));
    }
    state->finishChild();
}

}  // namespace detail

template <typename... Ts>
class WhenAllAwaiter : public CompositeWaitBase<WhenAllAwaiter<Ts...>> {
 public:
    explicit WhenAllAwaiter(std::tuple<Task<Ts>...> tasks,
                            std::shared_ptr<IExecutor> domain = nullptr)
        : tasks_(std::move(tasks)),
          domain_(std::move(domain)),
          state_(std::make_shared<WhenAllState<Ts...>>()) {
        state_->remaining = static_cast<int>(sizeof...(Ts));
    }

    ~WhenAllAwaiter() {
        this->disarmGate();
        group_.cancel();
    }

    bool await_ready() const noexcept {
        return sizeof...(Ts) == 0;
    }

    auto await_resume() {
        if (state_->exception) {
            std::rethrow_exception(state_->exception);
        }
        if constexpr ((!std::is_void_v<Ts> && ...)) {
            return doCollect(std::index_sequence_for<Ts...>{});
        }
    }

 private:
    friend class CompositeWaitBase<WhenAllAwaiter<Ts...>>;

    void start(const std::shared_ptr<ResumeGate>& gate, const CancellationToken* inherited,
               const std::shared_ptr<IExecutor>& awaitingDomain) {
        state_->setContinuationGate(gate);
        startAll(domain_ ? domain_ : awaitingDomain, inherited, std::index_sequence_for<Ts...>{});
    }

    template <size_t... Is>
    auto doCollect(std::index_sequence<Is...>) {
        return std::make_tuple(std::move(*std::get<Is>(state_->results))...);
    }

    template <size_t I>
    void startOne(const std::shared_ptr<IExecutor>& domain, const CancellationToken* inherited) {
        auto child = detail::whenAllChild<I>(state_, std::move(std::get<I>(tasks_)), group_);
        attach(child.getHandle().promise(), SpawnEdge{}, &group_, inherited);
        const bool dispatch = domain && !domain->isInline();
        if (dispatch) {
            child.setExecutor(domain);
        }
        auto handle = child.getHandle();
        auto frameControl = handle.promise().frameControl();
        childTasks_.push_back(std::move(child));

        if (dispatch) {
            const bool accepted = domain->tryExecute(
                [frameControl, handle]() noexcept { frameControl->resume(handle); });
            if (!accepted) {
                state_->childRefused();
            }
        } else {
            childTasks_.back().resume();
        }
    }

    template <size_t... Is>
    void startAll(const std::shared_ptr<IExecutor>& domain, const CancellationToken* inherited,
                  std::index_sequence<Is...>) {
        (startOne<Is>(domain, inherited), ...);
    }

    std::tuple<Task<Ts>...> tasks_;
    std::shared_ptr<IExecutor> domain_;
    std::shared_ptr<WhenAllState<Ts...>> state_;
    CancellationToken group_;
    std::vector<Task<void>> childTasks_;
};

template <typename... Ts>
auto when_all(Task<Ts>... tasks) {
    return WhenAllAwaiter<Ts...>(std::make_tuple(std::move(tasks)...));
}

// Owning: the awaiter keeps the domain alive while its children are parked.
template <typename... Ts>
auto when_all(std::shared_ptr<IExecutor> exec, Task<Ts>... tasks) {
    return WhenAllAwaiter<Ts...>(std::make_tuple(std::move(tasks)...), std::move(exec));
}

// Weak: the awaiter does not keep the executor alive; once it is gone, children are refused.
template <std::derived_from<IExecutor> E, typename... Ts>
auto when_all(std::weak_ptr<E> exec, Task<Ts>... tasks) {
    return when_all(
        std::shared_ptr<IExecutor>(std::make_shared<detail::WeakExecutor>(std::move(exec))),
        std::move(tasks)...);
}

template <typename... Ts>
struct WhenAnyResult {
    size_t winnerIndex = 0;
    std::variant<WhenAllResultType<Ts>...> winnerValue;
};

template <typename... Ts>
struct WhenAnyState : detail::WhenContinuationState {
    std::tuple<std::optional<WhenAllResultType<Ts>>...> results;
    std::atomic<int> completed{0};
    std::atomic<size_t> winnerIndex{0};
};

namespace detail {

template <size_t I, typename... Ts>
Task<void> whenAnyChild(std::shared_ptr<WhenAnyState<Ts...>> state,
                        Task<std::tuple_element_t<I, std::tuple<Ts...>>> task) {
    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
    std::exception_ptr error;
    try {
        if constexpr (!std::is_void_v<T>) {
            auto value = co_await std::move(task);
            std::get<I>(state->results).emplace(std::move(value));
        } else {
            co_await std::move(task);
            std::get<I>(state->results).emplace(VoidResult{});
        }
    } catch (...) {
        error = std::current_exception();
    }

    auto prev = state->completed.fetch_add(1, std::memory_order_acq_rel);
    if (prev == 0) {
        // First finisher is the winner: record index and exception, wake the parent coroutine
        state->winnerIndex.store(I, std::memory_order_relaxed);
        if (error) {
            state->recordException(error);
        }
        if (auto gate = state->takeContinuationGate()) {
            gate->signal();
        }
    }
    if (error) {
        std::rethrow_exception(error);
    }
}

}  // namespace detail

template <typename... Ts>
class WhenAnyAwaiter : public CompositeWaitBase<WhenAnyAwaiter<Ts...>> {
 public:
    static_assert(sizeof...(Ts) > 0, "when_any requires at least one task");

    WhenAnyAwaiter(AsyncScope& scope, std::tuple<Task<Ts>...> tasks)
        : scope_(scope),
          tasks_(std::move(tasks)),
          state_(std::make_shared<WhenAnyState<Ts...>>()) {}

    bool await_ready() const noexcept {
        return false;
    }

    auto await_resume() {
        if (state_->exception) {
            std::rethrow_exception(state_->exception);
        }
        auto index = state_->winnerIndex.load(std::memory_order_relaxed);
        return WhenAnyResult<Ts...>{index,
                                    buildWinnerValue(index, std::index_sequence_for<Ts...>{})};
    }

 private:
    friend class CompositeWaitBase<WhenAnyAwaiter<Ts...>>;

    void start(const std::shared_ptr<ResumeGate>& gate, const CancellationToken* inherited,
               const std::shared_ptr<IExecutor>&) {
        state_->setContinuationGate(gate);
        const std::size_t accepted = startAll(inherited, std::index_sequence_for<Ts...>{});
        if (accepted == 0) {
            state_->recordException(
                std::make_exception_ptr(CancelledException("executor refused every child task")));
            gate->signal();
        }
    }

    template <size_t I>
    bool startOne(const CancellationToken* inherited) {
        auto child = detail::whenAnyChild<I>(state_, std::move(std::get<I>(tasks_)));
        return inherited != nullptr ? scope_.spawn(std::move(child), *inherited)
                                    : scope_.spawn(std::move(child));
    }

    template <size_t... Is>
    std::size_t startAll(const CancellationToken* inherited, std::index_sequence<Is...>) {
        std::size_t accepted = 0;
        ((accepted += startOne<Is>(inherited) ? 1 : 0), ...);
        return accepted;
    }

    // Build the variant only from the winner index's result; other optionals are not read
    template <size_t... Is>
    auto buildWinnerValue(size_t index, std::index_sequence<Is...>) {
        std::variant<WhenAllResultType<Ts>...> value;
        (
            [&] {
                if (index == Is) {
                    value.template emplace<Is>(std::move(*std::get<Is>(state_->results)));
                }
            }(),
            ...);
        return value;
    }

    AsyncScope& scope_;
    std::tuple<Task<Ts>...> tasks_;
    std::shared_ptr<WhenAnyState<Ts...>> state_;
};

template <typename... Ts>
auto when_any(AsyncScope& scope, Task<Ts>... tasks) {
    return WhenAnyAwaiter<Ts...>(scope, std::make_tuple(std::move(tasks)...));
}

}  // namespace slark::coro
