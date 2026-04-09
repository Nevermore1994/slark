//
// Created by Nevermore on 2025/4/1.
// Slark Coroutine
// Copyright (c) 2025 Nevermore All rights reserved.
//
#pragma once

#include "NonCopyable.h"

#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace slark {

template <typename T = void>
class Coroutine;

namespace detail {

struct FinalAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
        if (auto cont = h.promise().continuation(); cont) {
            return cont;
        }
        return std::noop_coroutine();
    }

    void await_resume() noexcept {}
};

template <typename T>
class CoroutinePromise {
public:
    CoroutinePromise() noexcept = default;

    Coroutine<T> get_return_object() noexcept;

    std::suspend_always initial_suspend() noexcept {
        return {};
    }

    FinalAwaiter final_suspend() noexcept {
        return {};
    }

    void return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        value_.emplace(std::move(value));
    }

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
    }

    T getResult() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        return std::move(*value_);
    }

    bool hasResult() const noexcept {
        return value_.has_value();
    }

    bool hasException() const noexcept {
        return exception_ != nullptr;
    }

    void setContinuation(std::coroutine_handle<> cont) noexcept {
        continuation_ = cont;
    }

    std::coroutine_handle<> continuation() const noexcept {
        return continuation_;
    }

    void setCancelled() noexcept {
        cancelled_.store(true, std::memory_order_release);
    }

    bool isCancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

private:
    std::optional<T> value_;
    std::exception_ptr exception_;
    std::coroutine_handle<> continuation_;
    std::atomic<bool> cancelled_{false};
};

template <>
class CoroutinePromise<void> {
public:
    CoroutinePromise() noexcept = default;

    Coroutine<void> get_return_object() noexcept;

    std::suspend_always initial_suspend() noexcept {
        return {};
    }

    FinalAwaiter final_suspend() noexcept {
        return {};
    }

    void return_void() noexcept {}

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
    }

    void getResult() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    bool hasException() const noexcept {
        return exception_ != nullptr;
    }

    void setContinuation(std::coroutine_handle<> cont) noexcept {
        continuation_ = cont;
    }

    std::coroutine_handle<> continuation() const noexcept {
        return continuation_;
    }

    void setCancelled() noexcept {
        cancelled_.store(true, std::memory_order_release);
    }

    bool isCancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

private:
    std::exception_ptr exception_;
    std::coroutine_handle<> continuation_;
    std::atomic<bool> cancelled_{false};
};

} // namespace detail

template <typename T>
class [[nodiscard]] Coroutine : public NonCopyable {
public:
    using promise_type = detail::CoroutinePromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit Coroutine(handle_type handle) noexcept : handle_(handle) {}

    Coroutine(Coroutine&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Coroutine& operator=(Coroutine&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~Coroutine() {
        if (handle_) {
            handle_.destroy();
        }
    }

    bool isValid() const noexcept {
        return handle_ != nullptr;
    }

    bool isDone() const noexcept {
        return handle_ && handle_.done();
    }

    void resume() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    T getResult() {
        if (!handle_) {
            throw std::runtime_error("Coroutine is not valid");
        }
        return handle_.promise().getResult();
    }

    void cancel() noexcept {
        if (handle_) {
            handle_.promise().setCancelled();
        }
    }

    bool isCancelled() const noexcept {
        return handle_ && handle_.promise().isCancelled();
    }

    handle_type getHandle() const noexcept {
        return handle_;
    }

    handle_type release() noexcept {
        return std::exchange(handle_, nullptr);
    }

    // co_await 支持（左值引用）
    auto operator co_await() & noexcept {
        struct Awaiter {
            handle_type handle;

            bool await_ready() const noexcept {
                return handle.done();
            }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
                handle.promise().setContinuation(awaiting);
                return handle;
            }

            T await_resume() {
                return handle.promise().getResult();
            }
        };

        return Awaiter{handle_};
    }

    // co_await 支持（右值引用，支持 co_await someFunc() 临时对象）
    // 右值版本必须转移所有权，避免临时对象析构时销毁句柄
    auto operator co_await() && noexcept {
        struct Awaiter {
            handle_type handle;

            bool await_ready() const noexcept {
                return handle.done();
            }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
                handle.promise().setContinuation(awaiting);
                return handle;
            }

            T await_resume() {
                return handle.promise().getResult();
            }
        };

        // 转移所有权到 Awaiter，防止临时对象析构时销毁句柄
        return Awaiter{std::exchange(handle_, nullptr)};
    }

private:
    handle_type handle_;
};

namespace detail {

template <typename T>
Coroutine<T> CoroutinePromise<T>::get_return_object() noexcept {
    return Coroutine<T>{std::coroutine_handle<CoroutinePromise<T>>::from_promise(*this)};
}

inline Coroutine<void> CoroutinePromise<void>::get_return_object() noexcept {
    return Coroutine<void>{std::coroutine_handle<CoroutinePromise<void>>::from_promise(*this)};
}

} // namespace detail

} // namespace slark
