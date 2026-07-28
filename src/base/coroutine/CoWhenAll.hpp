// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "Task.hpp"

namespace slark::coro {

struct VoidResult {};

template <typename T>
using WhenAllResultType = std::conditional_t<std::is_void_v<T>, VoidResult, T>;

template <typename... Ts>
struct WhenAllState {
    std::tuple<std::optional<WhenAllResultType<Ts>>...> results;
    std::atomic<int> remaining{0};
    std::exception_ptr exception;
    std::coroutine_handle<> continuation;
    std::atomic<bool> parentResumed{false};
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
        if (!state->exception) {
            state->exception = std::current_exception();
        }
    }

    auto prev = state->remaining.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1 && state->continuation) {
        auto c = std::exchange(state->continuation, nullptr);
        state->parentResumed.store(true, std::memory_order_release);
        c.resume();
    }
}

}  // namespace detail

template <typename... Ts>
class WhenAllAwaiter {
 public:
    explicit WhenAllAwaiter(std::tuple<Task<Ts>...> tasks)
        : tasks_(std::move(tasks)), state_(std::make_shared<WhenAllState<Ts...>>()) {
        state_->remaining = static_cast<int>(sizeof...(Ts));
    }

    void scheduleVia(IExecutor& exec) noexcept {
        defaultExecutor_ = &exec;
    }

    bool await_ready() const noexcept {
        return sizeof...(Ts) == 0;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        state_->continuation = h;
        state_->parentResumed.store(false, std::memory_order_relaxed);

        IExecutor* exec = defaultExecutor_ ? defaultExecutor_ : h.promise().executor_;
        startTasks(exec, std::index_sequence_for<Ts...>{});

        return !state_->parentResumed.load(std::memory_order_acquire);
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
    void startOne(IExecutor* exec) {
        auto child = detail::whenAllChild<I>(state_, std::move(std::get<I>(tasks_)));
        auto handle = child.getHandle();
        childTasks_.push_back(std::move(child));

        if (exec && !exec->isInline()) {
            exec->execute([h = handle]() noexcept {
                h.resume();
            });
        } else {
            handle.resume();
        }
    }

    template <size_t... Is>
    void startTasks(IExecutor* exec, std::index_sequence<Is...>) {
        (startOne<Is>(exec), ...);
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
struct WhenAnyState {
    std::tuple<std::optional<WhenAllResultType<Ts>>...> results;
    std::atomic<int> completed{0};
    std::exception_ptr exception;
    std::coroutine_handle<> continuation;
    std::atomic<bool> parentResumed{false};
};

namespace detail {

template <size_t I, typename... Ts>
Task<void> whenAnyChild(std::shared_ptr<WhenAnyState<Ts...>> state,
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
        if (!state->exception) {
            state->exception = std::current_exception();
        }
    }

    auto prev = state->completed.fetch_add(1, std::memory_order_acq_rel);
    if (prev == 0 && state->continuation) {
        auto c = std::exchange(state->continuation, nullptr);
        state->parentResumed.store(true, std::memory_order_release);
        c.resume();
    }
}

}  // namespace detail

template <typename... Ts>
class WhenAnyAwaiter {
 public:
    explicit WhenAnyAwaiter(std::tuple<Task<Ts>...> tasks)
        : tasks_(std::move(tasks)), state_(std::make_shared<WhenAnyState<Ts...>>()) {}

    void scheduleVia(IExecutor& exec) noexcept {
        defaultExecutor_ = &exec;
    }

    bool await_ready() const noexcept {
        return sizeof...(Ts) == 0;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        state_->continuation = h;
        state_->parentResumed.store(false, std::memory_order_relaxed);
        IExecutor* exec = defaultExecutor_ ? defaultExecutor_ : h.promise().executor_;
        startTasks(exec, std::index_sequence_for<Ts...>{});
        return !state_->parentResumed.load(std::memory_order_acquire);
    }

    auto await_resume() {
        if constexpr ((!std::is_void_v<Ts> && ...)) {
            return doCollect(std::index_sequence_for<Ts...>{});
        }
    }

 private:
    template <size_t I>
    void startOne(IExecutor* exec) {
        auto child = detail::whenAnyChild<I>(state_, std::move(std::get<I>(tasks_)));
        auto handle = child.getHandle();
        childTasks_.push_back(std::move(child));

        if (exec && !exec->isInline()) {
            exec->execute([h = handle]() noexcept {
                h.resume();
            });
        } else {
            handle.resume();
        }
    }

    template <size_t... Is>
    void startTasks(IExecutor* exec, std::index_sequence<Is...>) {
        (startOne<Is>(exec), ...);
    }

    template <size_t... Is>
    auto doCollect(std::index_sequence<Is...>) {
        return std::make_tuple(std::move(*std::get<Is>(state_->results))...);
    }

    std::tuple<Task<Ts>...> tasks_;
    std::shared_ptr<WhenAnyState<Ts...>> state_;
    std::vector<Task<void>> childTasks_;
    IExecutor* defaultExecutor_ = nullptr;
};

template <typename... Ts>
auto when_any(Task<Ts>... tasks) {
    return WhenAnyAwaiter<Ts...>(std::make_tuple(std::move(tasks)...));
}

template <typename... Ts>
auto when_any(IExecutor& exec, Task<Ts>... tasks) {
    auto awaiter = WhenAnyAwaiter<Ts...>(std::make_tuple(std::move(tasks)...));
    awaiter.scheduleVia(exec);
    return awaiter;
}

}  // namespace slark::coro
