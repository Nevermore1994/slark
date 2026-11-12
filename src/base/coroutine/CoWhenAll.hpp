// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "CoScope.hpp"
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
    CancellationToken group;

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
                        Task<std::tuple_element_t<I, std::tuple<Ts...>>> task) {
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
            state->group.cancel();
        }
        state->recordException(std::move(error));
    }
    state->finishChild();
}

}  // namespace detail

template <typename... Ts>
class WhenAllAwaiter {
 public:
    explicit WhenAllAwaiter(std::tuple<Task<Ts>...> tasks)
        : tasks_(std::move(tasks)), state_(std::make_shared<WhenAllState<Ts...>>()) {
        state_->remaining = static_cast<int>(sizeof...(Ts));
    }

    WhenAllAwaiter(WhenAllAwaiter&&) noexcept = default;
    WhenAllAwaiter& operator=(WhenAllAwaiter&&) noexcept = default;
    WhenAllAwaiter(const WhenAllAwaiter&) = delete;
    WhenAllAwaiter& operator=(const WhenAllAwaiter&) = delete;

    ~WhenAllAwaiter() {
        if (state_) {
            state_->group.cancel();
        }
    }

    void scheduleVia(IExecutor& exec) noexcept {
        defaultExecutor_ = &exec;
    }

    bool await_ready() const noexcept {
        return sizeof...(Ts) == 0;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        releaseTransferLeases(h.promise());
        markExternalAwait(h.promise());
        auto gate = makeResumeGate(h);
        state_->setContinuationGate(gate);

        IExecutor* exec = defaultExecutor_ ? defaultExecutor_ : h.promise().executor_.get();
        startTasks(exec, h.promise().cancelToken(), std::index_sequence_for<Ts...>{});

        return gate->arm();
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
    template <size_t... Is>
    auto doCollect(std::index_sequence<Is...>) {
        return std::make_tuple(std::move(*std::get<Is>(state_->results))...);
    }

    template <size_t I>
    void startOne(IExecutor* exec, const CancellationToken& token) {
        auto child = detail::whenAllChild<I>(state_, std::move(std::get<I>(tasks_)));
        child.setCancellationToken(token);
        auto handle = child.getHandle();
        auto frameControl = handle.promise().frameControl();
        childTasks_.push_back(std::move(child));

        if (exec && !exec->isInline()) {
            const bool accepted = exec->tryExecute(
                [frameControl, handle]() noexcept { frameControl->resume(handle); });
            if (!accepted) {
                state_->childRefused();
            }
        } else {
            childTasks_.back().resume();
        }
    }

    template <size_t... Is>
    void startTasks(IExecutor* exec, const CancellationToken* parent,
                    std::index_sequence<Is...>) {
        const CancellationToken token =
            parent ? CancellationToken::link(state_->group, *parent) : state_->group;
        (startOne<Is>(exec, token), ...);
    }

    std::tuple<Task<Ts>...> tasks_;
    std::shared_ptr<WhenAllState<Ts...>> state_;
    std::vector<Task<void>> childTasks_;
    IExecutor* defaultExecutor_ = nullptr;
};

template <typename... Ts>
auto when_all(Task<Ts>... tasks) {
    return WhenAllAwaiter<Ts...>(std::make_tuple(std::move(tasks)...));
}

template <typename... Ts>
auto when_all(IExecutor& exec, Task<Ts>... tasks) {
    auto awaiter = WhenAllAwaiter<Ts...>(std::make_tuple(std::move(tasks)...));
    awaiter.scheduleVia(exec);
    return awaiter;
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
class WhenAnyAwaiter {
 public:
    static_assert(sizeof...(Ts) > 0, "when_any requires at least one task");

    WhenAnyAwaiter(AsyncScope& scope, std::tuple<Task<Ts>...> tasks)
        : scope_(scope),
          tasks_(std::move(tasks)),
          state_(std::make_shared<WhenAnyState<Ts...>>()) {}

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        releaseTransferLeases(h.promise());
        markExternalAwait(h.promise());
        auto gate = makeResumeGate(h);
        state_->setContinuationGate(gate);
        const std::size_t accepted =
            startTasks(childToken(h.promise()), std::index_sequence_for<Ts...>{});
        if (accepted == 0) {
            state_->recordException(
                std::make_exception_ptr(CancelledException("executor refused every child task")));
            gate->signal();
        }
        return gate->arm();
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
    template <typename Promise>
    [[nodiscard]] CancellationToken childToken(const Promise& promise) const {
        auto token = scope_.token();
        if (const auto* parent = promise.cancelToken()) {
            return CancellationToken::link(token, *parent);
        }
        return token;
    }

    template <size_t I>
    bool startOne(const CancellationToken& token) {
        return scope_.spawn(detail::whenAnyChild<I>(state_, std::move(std::get<I>(tasks_))),
                            token);
    }

    template <size_t... Is>
    std::size_t startTasks(const CancellationToken& token, std::index_sequence<Is...>) {
        std::size_t accepted = 0;
        ((accepted += startOne<Is>(token) ? 1 : 0), ...);
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
