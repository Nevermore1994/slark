// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "CoroutineControl.hpp"
#include "NonCopyable.h"

namespace slark {

struct IExecutor;

template <typename T = void>
class Task;

struct TaskPromiseBase {
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

            if (!continuation.isValid()) {
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

    std::suspend_always initial_suspend() noexcept {
        return {};
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

    template <typename ParentPromise>
        requires ContinuationPromise<ParentPromise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept {
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

        auto link = ContinuationLink::create(awaiting, awaiting.promise().frameControl(),
                                             awaiting.promise().continuationLeaseSlot());
        if (!link.isValid()) {
            childLease_.reset();
            return awaiting;
        }
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
    // domain of their own.
    void setExecutor(std::shared_ptr<IExecutor> executor) noexcept {
        if (handle_) {
            handle_.promise().setExecutor(std::move(executor));
        }
    }

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

}  // namespace slark
