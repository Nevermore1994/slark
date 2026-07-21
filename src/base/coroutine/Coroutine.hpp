//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NonCopyable.h"
#include "CoroutineScheduler.hpp"
#include "SharedCoroutineHandle.hpp"

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

struct PromiseBase {
    std::shared_ptr<SharedCoroutineHandle> continuation_;
    std::weak_ptr<CoroutineControlBlock> control_;
    std::atomic<bool> completed{false};

    void setControl(std::shared_ptr<CoroutineControlBlock> control) noexcept {
        control_ = std::move(control);
    }

    [[nodiscard]] std::shared_ptr<SharedCoroutineHandle> makeResumeHandle() const noexcept {
        auto control = control_.lock();
        if (!control) {
            return nullptr;
        }
        return std::make_shared<SharedCoroutineHandle>(std::move(control));
    }

    void setContinuation(std::shared_ptr<SharedCoroutineHandle> continuation) noexcept {
        continuation_ = std::move(continuation);
    }

    [[nodiscard]] std::shared_ptr<SharedCoroutineHandle> takeContinuation() noexcept {
        return std::exchange(continuation_, nullptr);
    }

    void markCompleted() noexcept {
        completed.store(true, std::memory_order_release);
        if (auto control = control_.lock()) {
            control->markCompleted();
        }
    }

    void setCancelled() noexcept {
        if (auto control = control_.lock()) {
            control->cancel();
        }
    }

    [[nodiscard]] bool isCancelled() const noexcept {
        if (auto control = control_.lock()) {
            return control->isCancelled();
        }
        return false;
    }
};

template <typename T>
class CoroutinePromise : public PromiseBase {
public:
    CoroutinePromise() noexcept = default;
    ~CoroutinePromise() noexcept = default;

    Coroutine<T> get_return_object() noexcept;

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            auto& promise = handle.promise();
            promise.markCompleted();
            if (auto continuation = promise.takeContinuation(); continuation && continuation->notify()) {
                CoroutineScheduler::instance().schedule(std::move(continuation));
            }
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        value_.emplace(std::move(value));
    }

    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    T getResult() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        return std::move(*value_);
    }

    [[nodiscard]] bool hasException() const noexcept { return exception_ != nullptr; }

private:
    std::optional<T> value_;
    std::exception_ptr exception_;
};

template <>
class CoroutinePromise<void> : public PromiseBase {
public:
    CoroutinePromise() noexcept = default;
    ~CoroutinePromise() noexcept = default;

    Coroutine<void> get_return_object() noexcept;

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            auto& promise = handle.promise();
            promise.markCompleted();
            if (auto continuation = promise.takeContinuation(); continuation && continuation->notify()) {
                CoroutineScheduler::instance().schedule(std::move(continuation));
            }
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_void() noexcept {}

    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    void getResult() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    [[nodiscard]] bool hasException() const noexcept { return exception_ != nullptr; }

private:
    std::exception_ptr exception_;
};

template <typename T>
class CoroutineAwaiter {
public:
    using promise_type = CoroutinePromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    CoroutineAwaiter(std::shared_ptr<CoroutineControlBlock> control, handle_type handle) noexcept
        : control_(std::move(control)), handle_(handle) {}

    bool await_ready() const noexcept { return !control_ || control_->isDone(); }

    template <typename ParentPromise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> awaiting) noexcept {
        if (auto parent = awaiting.promise().makeResumeHandle()) {
            (void)parent->suspend();
            handle_.promise().setContinuation(std::move(parent));
        }
        return handle_;
    }

    T await_resume() { return handle_.promise().getResult(); }

private:
    std::shared_ptr<CoroutineControlBlock> control_;
    handle_type handle_ = nullptr;
};

} // namespace detail

template <typename T>
class [[nodiscard]] Coroutine : public NonCopyable {
public:
    using promise_type = detail::CoroutinePromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit Coroutine(handle_type handle) noexcept : handle_(handle) {
        if (handle_) {
            control_ = std::make_shared<CoroutineControlBlock>(handle_);
            handle_.promise().setControl(control_);
        }
    }

    Coroutine(Coroutine&& other) noexcept
        : control_(std::move(other.control_)), handle_(std::exchange(other.handle_, nullptr)) {}

    Coroutine& operator=(Coroutine&& other) noexcept {
        if (this != &other) {
            control_ = std::move(other.control_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~Coroutine() = default;

    SharedCoroutineHandle share() && noexcept {
        auto control = std::move(control_);
        handle_ = nullptr;
        return SharedCoroutineHandle(std::move(control));
    }

    [[nodiscard]] bool isValid() const noexcept { return control_ != nullptr; }

    [[nodiscard]] bool isDone() const noexcept { return !control_ || control_->isDone(); }

    void resume() noexcept {
        if (control_) {
            control_->resume();
        }
    }

    T getResult() {
        if (!handle_) {
            throw std::runtime_error("Coroutine is not valid");
        }
        return handle_.promise().getResult();
    }

    void cancel() noexcept {
        if (control_) {
            control_->cancel();
        }
    }

    [[nodiscard]] bool isCancelled() const noexcept {
        return control_ && control_->isCancelled();
    }

    [[nodiscard]] handle_type getHandle() const noexcept { return handle_; }

    [[nodiscard]] std::shared_ptr<SharedCoroutineHandle> makeResumeHandle() const noexcept {
        if (!control_) {
            return nullptr;
        }
        return std::make_shared<SharedCoroutineHandle>(control_);
    }

    auto operator co_await() const& noexcept {
        return detail::CoroutineAwaiter<T>{control_, handle_};
    }

    auto operator co_await() && noexcept {
        return detail::CoroutineAwaiter<T>{std::move(control_), std::exchange(handle_, nullptr)};
    }

private:
    std::shared_ptr<CoroutineControlBlock> control_;
    handle_type handle_ = nullptr;
};

namespace detail {

template <typename T>
inline Coroutine<T> CoroutinePromise<T>::get_return_object() noexcept {
    auto handle = std::coroutine_handle<CoroutinePromise<T>>::from_promise(*this);
    return Coroutine<T>(handle);
}

inline Coroutine<void> CoroutinePromise<void>::get_return_object() noexcept {
    auto handle = std::coroutine_handle<CoroutinePromise<void>>::from_promise(*this);
    return Coroutine<void>(handle);
}

} // namespace detail

} // namespace slark
