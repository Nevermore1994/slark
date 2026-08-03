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

#include "Executor.h"
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
    std::mutex mutex;
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
        if (!exception) {
            exception = std::move(error);
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
        state->recordException(std::current_exception());
    }

    auto prev = state->remaining.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        if (auto gate = state->takeContinuationGate()) {
            gate->signal();
        }
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
        auto gate = makeResumeGate(h);
        state_->setContinuationGate(gate);

        IExecutor* exec = defaultExecutor_ ? defaultExecutor_ : h.promise().executor_;
        startTasks(exec, std::index_sequence_for<Ts...>{});

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
    void startOne(IExecutor* exec) {
        auto child = detail::whenAllChild<I>(state_, std::move(std::get<I>(tasks_)));
        auto handle = child.getHandle();
        auto frameControl = handle.promise().frameControl();
        childTasks_.push_back(std::move(child));

        if (exec && !exec->isInline()) {
            exec->execute([frameControl, handle]() noexcept {
                frameControl->resume(handle);
            });
        } else {
            childTasks_.back().resume();
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
struct WhenAnyResult {
    size_t winnerIndex = 0;
    std::variant<WhenAllResultType<Ts>...> winnerValue;
};

template <typename... Ts>
struct WhenAnyState {
    std::tuple<std::optional<WhenAllResultType<Ts>>...> results;
    std::atomic<int> completed{0};
    std::atomic<size_t> winnerIndex{0};
    std::exception_ptr exception;
    std::mutex mutex;
    std::shared_ptr<ResumeGate> continuationGate;

    std::mutex orphanMutex;
    std::vector<Task<void>> orphanTasks;

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
        if (!exception) {
            exception = std::move(error);
        }
    }
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
        // 首个完成者是 winner：记录索引与异常，唤醒父协程
        state->winnerIndex.store(I, std::memory_order_relaxed);
        if (error) {
            state->recordException(std::move(error));
        }
        if (auto gate = state->takeContinuationGate()) {
            gate->signal();
        }
    }
    // loser 的结果保活在 results 中但从不读取；loser 异常吞掉，不影响 winner
}

}  // namespace detail

template <typename... Ts>
class WhenAnyAwaiter {
 public:
    static_assert(sizeof...(Ts) > 0, "when_any requires at least one task");

    explicit WhenAnyAwaiter(std::tuple<Task<Ts>...> tasks)
        : tasks_(std::move(tasks)), state_(std::make_shared<WhenAnyState<Ts...>>()) {}

    void scheduleVia(IExecutor& exec) noexcept {
        defaultExecutor_ = &exec;
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        auto gate = makeResumeGate(h);
        state_->setContinuationGate(gate);
        IExecutor* exec = defaultExecutor_ ? defaultExecutor_ : h.promise().executor_;
        startTasks(exec, std::index_sequence_for<Ts...>{});
        return gate->arm();
    }

    auto await_resume() {
        if (state_->exception) {
            std::rethrow_exception(state_->exception);
        }
        auto index = state_->winnerIndex.load(std::memory_order_relaxed);

        if (childTasks_.size() > 1) {
            std::lock_guard lock(state_->orphanMutex);
            for (size_t i = 0; i < childTasks_.size(); ++i) {
                if (i != index) {
                    state_->orphanTasks.push_back(std::move(childTasks_[i]));
                }
            }
        }

        return WhenAnyResult<Ts...>{index, buildWinnerValue(index, std::index_sequence_for<Ts...>{})};
    }

 private:
    template <size_t I>
    void startOne(IExecutor* exec) {
        auto child = detail::whenAnyChild<I>(state_, std::move(std::get<I>(tasks_)));
        auto handle = child.getHandle();
        auto frameControl = handle.promise().frameControl();
        childTasks_.push_back(std::move(child));

        if (exec && !exec->isInline()) {
            exec->execute([frameControl, handle]() noexcept {
                frameControl->resume(handle);
            });
        } else {
            childTasks_.back().resume();
        }
    }

    template <size_t... Is>
    void startTasks(IExecutor* exec, std::index_sequence<Is...>) {
        (startOne<Is>(exec), ...);
    }

    // 仅从 winner 索引对应结果构造 variant，其余 optional 不读取
    template <size_t... Is>
    auto buildWinnerValue(size_t index, std::index_sequence<Is...>) {
        std::variant<WhenAllResultType<Ts>...> value;
        ([&] {
            if (index == Is) {
                value.template emplace<Is>(std::move(*std::get<Is>(state_->results)));
            }
        }(), ...);
        return value;
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
