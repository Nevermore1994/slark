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
    std::coroutine_handle<> continuation_;
    IExecutor* executor_ = nullptr;
    std::shared_ptr<CoroutineFrameControl> frameControl_ =
        std::make_shared<CoroutineFrameControl>();

    struct FinalAwaiter {
        bool await_ready() const noexcept {
            return false;
        }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
            auto& promise = h.promise();
            if (!promise.continuation_) {
                return std::noop_coroutine();
            }

            auto continuation = promise.continuation_;
            promise.continuation_ = nullptr;
            return continuation;
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept {
        return {};
    }

    std::suspend_always initial_suspend() noexcept {
        return {};
    }

    void setContinuation(std::coroutine_handle<> cont) noexcept {
        continuation_ = cont;
    }

    [[nodiscard]] std::shared_ptr<CoroutineFrameControl> frameControl() const noexcept {
        return frameControl_;
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

    TaskAwaiter(TaskAwaiter&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    TaskAwaiter& operator=(TaskAwaiter&& other) noexcept {
        if (this != &other) {
            detail::destroyTaskFrame(std::exchange(handle_, nullptr));
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~TaskAwaiter() {
        detail::destroyTaskFrame(std::exchange(handle_, nullptr));
    }

    bool await_ready() const noexcept {
        return !handle_ || handle_.done();
    }

    template <typename ParentPromise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept {
        auto& childPromise = handle_.promise();
        childPromise.setContinuation(awaiting);
        if constexpr (requires { awaiting.promise().executor_; }) {
            childPromise.executor_ = awaiting.promise().executor_;
        }
        return handle_;
    }

    T await_resume() {
        return handle_.promise().result();
    }

 private:
    handle_type handle_ = nullptr;
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
        return !handle_ || handle_.done();
    }

    void resume() noexcept {
        if (handle_) {
            handle_.promise().frameControl()->resume(handle_);
        }
    }

    T result() {
        if (!handle_) {
            throw std::runtime_error("Task is not valid");
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
