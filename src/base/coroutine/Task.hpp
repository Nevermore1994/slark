// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "CoroutineControl.hpp"
#include "Error.h"
#include "NonCopyable.h"

namespace slark {

struct IExecutor;

template <typename T = void>
class Task;

struct TaskPromiseBase : PromiseCancellation {
    ContinuationLink continuation_;
    // Owned, not borrowed: a resume may be dispatched long after the code that bound it,
    // from whichever thread notified the gate.
    std::shared_ptr<IExecutor> executor_;
    std::shared_ptr<CoroutineFrameControl> frameControl_ =
        std::make_shared<CoroutineFrameControl>(true);

 private:
    ExecutionLease continuationLease_;
    ExecutionLease incomingTransferLease_;

 public:
    struct FinalAwaiter {
        bool await_ready() const noexcept {
            return false;
        }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
            auto& promise = h.promise();
            const auto control = promise.frameControl_;
            releaseTransferLeases(promise);
            ContinuationLink continuation;
            control->finish([&] {
                continuation = std::move(promise.continuation_);
            });

            if (!continuation.isRegistered() || !continuation.acquireTarget()) {
                continuation.reset();
                return std::noop_coroutine();
            }
            *continuation.targetLeaseSlot = std::move(continuation.lease);
            auto target = continuation.handle;
            continuation.handle = nullptr;
            continuation.control.reset();
            return target;
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept {
        frameControl_->markFinalizing();
        return {};
    }

    FrameStartAwaiter<TaskPromiseBase> initial_suspend() noexcept {
        return {this};
    }

    void releaseIncomingTransferLease() noexcept {
        incomingTransferLease_.reset();
    }

    void releaseContinuationLease() noexcept {
        continuationLease_.reset();
    }

    void releaseContinuation() noexcept {
        continuation_.reset();
    }

    [[nodiscard]] ExecutionLease* continuationLeaseSlot() noexcept {
        return &continuationLease_;
    }

    [[nodiscard]] ExecutionLease* incomingTransferLeaseSlot() noexcept {
        return &incomingTransferLease_;
    }

    [[nodiscard]] std::shared_ptr<CoroutineFrameControl> frameControl() const noexcept {
        return frameControl_;
    }

    // Execution domain this coroutine must be resumed on. Null means "resume on the
    // notifying thread", which is the historical behaviour.
    [[nodiscard]] const std::shared_ptr<IExecutor>& executor() const noexcept {
        return executor_;
    }

    void setExecutor(std::shared_ptr<IExecutor> executor) noexcept {
        executor_ = std::move(executor);
    }
};

template <typename T>
class TaskPromise : public TaskPromiseBase {
 public:
    Task<T> get_return_object() noexcept;

    void return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        value_.emplace(std::move(value));
    }

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
    }

    T result() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        return std::move(*value_);
    }

 private:
    std::optional<T> value_;
    std::exception_ptr exception_;
};

template <>
class TaskPromise<void> : public TaskPromiseBase {
 public:
    Task<void> get_return_object() noexcept;

    void return_void() noexcept {}

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
    }

    void result() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

 private:
    std::exception_ptr exception_;
};

namespace detail {
template <typename Handle>
void destroyTaskFrame(Handle handle) noexcept {
    if (!handle) {
        return;
    }
    if (handle.promise().frameControl()->destroy(handle)) {
        handle.destroy();
    }
}

template <typename T, typename F>
struct TaskInvokeResult : std::invoke_result<F, T> {};

template <typename F>
struct TaskInvokeResult<void, F> : std::invoke_result<F> {};
}  // namespace detail

template <typename T>
class TaskAwaiter {
 public:
    using promise_type = TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    TaskAwaiter() noexcept = default;

    explicit TaskAwaiter(handle_type handle) noexcept : handle_(handle) {}

    TaskAwaiter(TaskAwaiter&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
          childLease_(std::move(other.childLease_)) {}

    TaskAwaiter& operator=(TaskAwaiter&& other) noexcept {
        if (this != &other) {
            destroyChild();
            handle_ = std::exchange(other.handle_, nullptr);
            childLease_ = std::move(other.childLease_);
        }
        return *this;
    }

    ~TaskAwaiter() {
        destroyChild();
    }

    bool await_ready() const noexcept {
        return !handle_ || handle_.promise().frameControl()->isDone(handle_);
    }

    // A child that was already resumed is parked on its own wait; entering it here would
    // resume that wait without its event. Checked before any lease is taken or released.
    template <typename ParentPromise>
        requires ContinuationPromise<ParentPromise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) {
        if (handle_ && handle_.promise().hasStarted()) {
            throw std::logic_error("co_await on a Task that was already resumed");
        }
        releaseTransferLeases(awaiting.promise());
        if (!handle_) {
            return awaiting;
        }

        auto& childPromise = handle_.promise();
        const auto childControl = childPromise.frameControl();
        childLease_ = childControl->tryAcquire(handle_);
        if (!childLease_) {
            return awaiting;
        }

        auto link = ContinuationLink::registerTarget(awaiting, awaiting.promise().frameControl(),
                                                     awaiting.promise().continuationLeaseSlot());
        if (!link.isRegistered()) {
            childLease_.reset();
            return awaiting;
        }
        inheritCancellation(childPromise, awaiting.promise());
        // Local/explicit domain wins; the parent's domain is only a fallback.
        if constexpr (requires { awaiting.promise().executor(); }) {
            if (childPromise.executor_ == nullptr) {
                childPromise.executor_ = awaiting.promise().executor();
            }
        }
        const auto handle = handle_;
        const auto phase = childControl->linkUnlessFinished([&] {
            childPromise.continuation_ = std::move(link);
        });
        if (phase == CoroutineFrameControl::Phase::Finished) {
            return awaiting;
        }
        if (phase == CoroutineFrameControl::Phase::Finalizing) {
            return std::noop_coroutine();
        }
        return handle;
    }

    T await_resume() {
        if (!handle_) {
            throw std::runtime_error("Task is not valid");
        }
        auto& childPromise = handle_.promise();
        childPromise.releaseContinuation();
        childLease_.reset();
        return childPromise.result();
    }

 private:
    void destroyChild() noexcept {
        if (!handle_) {
            childLease_.reset();
            return;
        }
        auto handle = handle_;
        bool destroyNow = handle.promise().frameControl()->destroy(handle);
        childLease_.reset();
        handle_ = nullptr;
        if (destroyNow) {
            handle.destroy();
        }
    }

    handle_type handle_ = nullptr;
    ExecutionLease childLease_;
};

template <typename T>
class [[nodiscard]] Task : public NonCopyable {
 public:
    using promise_type = TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    Task() noexcept = default;

    explicit Task(handle_type handle) noexcept : handle_(handle) {}

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            detail::destroyTaskFrame(std::exchange(handle_, nullptr));
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~Task() {
        detail::destroyTaskFrame(std::exchange(handle_, nullptr));
    }

    [[nodiscard]] bool isValid() const noexcept {
        return handle_ != nullptr;
    }

    [[nodiscard]] bool isDone() const noexcept {
        return !handle_ || handle_.promise().frameControl()->isDone(handle_);
    }

    void resume() noexcept {
        if (handle_) {
            handle_.promise().frameControl()->resume(handle_);
        }
    }

    // Binds the root task to an execution domain; propagated to child tasks that have no
    // domain of their own. Must precede the first resume.
    void setExecutor(std::shared_ptr<IExecutor> executor) {
        if (handle_) {
            detail::requireNotStarted(handle_.promise(), "Task::setExecutor after the first resume");
            handle_.promise().setExecutor(std::move(executor));
        }
    }

    void setCancellationToken(CancellationToken token) {
        if (handle_) {
            detail::requireNotStarted(handle_.promise(),
                                      "Task::setCancellationToken after the first resume");
            handle_.promise().setCancelToken(std::move(token));
        }
    }

    template <typename F>
    auto then(F func) && -> Task<typename detail::TaskInvokeResult<T, F>::type>;

    template <typename F>
    auto and_then(F func) && -> typename detail::TaskInvokeResult<T, F>::type;

    template <typename F>
    Task<T> or_else(F func) &&;

    T result() {
        if (!handle_) {
            throw std::runtime_error("Task is not valid");
        }
        if (!isDone()) {
            throw std::runtime_error("Task is not complete");
        }
        return handle_.promise().result();
    }

    [[nodiscard]] handle_type getHandle() const noexcept {
        return handle_;
    }

    auto operator co_await() && noexcept {
        return TaskAwaiter<T>{std::exchange(handle_, nullptr)};
    }

 private:
    handle_type handle_ = nullptr;
};

template <typename T>
inline Task<T> TaskPromise<T>::get_return_object() noexcept {
    auto handle = std::coroutine_handle<TaskPromise<T>>::from_promise(*this);
    return Task<T>(handle);
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
    auto handle = std::coroutine_handle<TaskPromise<void>>::from_promise(*this);
    return Task<void>(handle);
}

namespace detail {

template <typename T>
void requireValidTask(const Task<T>& task) {
    if (!task.isValid()) {
        throw std::runtime_error("Task is not valid");
    }
}

template <typename T, typename F>
Task<typename TaskInvokeResult<T, F>::type> thenImpl(Task<T> task, F func) {
    requireValidTask(task);
    if constexpr (std::is_void_v<T>) {
        co_await std::move(task);
        co_return std::invoke(func);
    } else {
        co_return std::invoke(func, co_await std::move(task));
    }
}

template <typename T, typename F>
typename TaskInvokeResult<T, F>::type andThenImpl(Task<T> task, F func) {
    requireValidTask(task);
    if constexpr (std::is_void_v<T>) {
        co_await std::move(task);
        co_return co_await std::invoke(func);
    } else {
        co_return co_await std::invoke(func, co_await std::move(task));
    }
}

template <typename T, typename F>
Task<T> orElseImpl(Task<T> task, F func) {
    std::exception_ptr failure;
    try {
        requireValidTask(task);
        if constexpr (std::is_void_v<T>) {
            co_await std::move(task);
            co_return;
        } else {
            co_return co_await std::move(task);
        }
    } catch (const CancelledException&) {
        throw;
    } catch (...) {
        failure = std::current_exception();
    }
    if constexpr (std::is_void_v<T>) {
        std::invoke(func, failure);
        co_return;
    } else {
        co_return std::invoke(func, failure);
    }
}

}  // namespace detail

template <typename T>
template <typename F>
auto Task<T>::then(F func) && -> Task<typename detail::TaskInvokeResult<T, F>::type> {
    return detail::thenImpl(std::move(*this), std::move(func));
}

template <typename T>
template <typename F>
auto Task<T>::and_then(F func) && -> typename detail::TaskInvokeResult<T, F>::type {
    return detail::andThenImpl(std::move(*this), std::move(func));
}

template <typename T>
template <typename F>
Task<T> Task<T>::or_else(F func) && {
    return detail::orElseImpl(std::move(*this), std::move(func));
}

namespace coro {

class CancellationTokenAwaiter {
public:
    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        if (const auto* token = cancellationTokenOf(handle.promise())) {
            token_ = *token;
        }
        return false;
    }

    std::optional<CancellationToken> await_resume() noexcept {
        return std::move(token_);
    }

private:
    std::optional<CancellationToken> token_;
};

// Reads whether the running frame's cancellation has been requested. Never suspends.
class CancellationRequestedAwaiter {
public:
    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        if (const auto* token = cancellationTokenOf(handle.promise())) {
            cancelled_ = token->isCancelled();
        }
        return false;
    }

    bool await_resume() const noexcept {
        return cancelled_;
    }

private:
    bool cancelled_ = false;
};

inline CancellationRequestedAwaiter cancellation_requested() noexcept {
    return {};
}

inline CancellationTokenAwaiter cancellation_token() noexcept {
    return {};
}

}  // namespace coro

}  // namespace slark
