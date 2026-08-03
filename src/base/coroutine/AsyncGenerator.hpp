// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "NonCopyable.h"
#include "Task.hpp"

namespace slark {

template <typename T>
class AsyncGenerator;

namespace detail {

enum class ConsumerState : uint8_t { None, Waiting, Resumed };

template <typename Promise>
class AsyncGeneratorYieldAwaiter {
 public:
    explicit AsyncGeneratorYieldAwaiter(Promise& promise) noexcept : promise_(promise) {}

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept {
        if (auto consumer = promise_.takeWaitingConsumer()) {
            consumer->signal();
        }
        return std::noop_coroutine();
    }

    void await_resume() noexcept {}

 private:
    Promise& promise_;
};

template <typename T>
class AsyncGeneratorFinalAwaiter {
 public:
    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
        auto& promise = h.promise();
        promise.setCompleted();
        if (auto consumer = promise.takeWaitingConsumer()) {
            consumer->signal();
        }
        return std::noop_coroutine();
    }

    void await_resume() noexcept {}
};

template <typename T>
class AsyncGeneratorPromise {
 public:
    using value_type = T;
    using reference_type = T&;

    std::suspend_always initial_suspend() noexcept {
        return {};
    }

    AsyncGeneratorFinalAwaiter<T> final_suspend() noexcept {
        return {};
    }

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
        setCompleted();
    }

    void return_void() noexcept {
        setCompleted();
    }

    AsyncGeneratorYieldAwaiter<AsyncGeneratorPromise<T>> yield_value(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>) {
        value_.emplace(std::move(value));
        return AsyncGeneratorYieldAwaiter<AsyncGeneratorPromise<T>>{*this};
    }

    AsyncGenerator<T> get_return_object() noexcept;

    reference_type value() noexcept {
        return *value_;
    }

    bool hasValue() const noexcept {
        return value_.has_value();
    }

    void clearValue() noexcept {
        value_.reset();
    }

    void rethrowIfException() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    bool hasException() const noexcept {
        return exception_ != nullptr;
    }

    bool isCompleted() const noexcept {
        return completed_.load(std::memory_order_acquire);
    }

    void setCompleted() noexcept {
        completed_.store(true, std::memory_order_release);
    }

    void setWaitingConsumer(std::shared_ptr<ResumeGate> consumer) noexcept {
        consumerGate_ = std::move(consumer);
        consumerState_ = ConsumerState::Waiting;
    }

    std::shared_ptr<ResumeGate> takeWaitingConsumer() noexcept {
        if (consumerState_ == ConsumerState::Waiting) {
            consumerState_ = ConsumerState::Resumed;
            return std::exchange(consumerGate_, nullptr);
        }
        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<CoroutineFrameControl> frameControl() const noexcept {
        return frameControl_;
    }

 private:
    std::optional<T> value_;
    std::exception_ptr exception_;
    std::shared_ptr<ResumeGate> consumerGate_;
    std::shared_ptr<CoroutineFrameControl> frameControl_ =
        std::make_shared<CoroutineFrameControl>();
    ConsumerState consumerState_ = ConsumerState::None;
    std::atomic<bool> completed_{false};
};

template <typename T>
class AsyncGeneratorNextAwaiter {
 public:
    using promise_type = AsyncGeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    AsyncGeneratorNextAwaiter() noexcept = default;

    explicit AsyncGeneratorNextAwaiter(handle_type handle) noexcept : handle_(handle) {}

    bool await_ready() noexcept {
        if (!handle_) {
            return true;
        }
        auto& promise = handle_.promise();
        return promise.hasValue() || promise.isCompleted();
    }

    template <typename ConsumerPromise>
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<ConsumerPromise> consumer) noexcept {
        auto& promise = handle_.promise();
        if (promise.hasValue() || promise.isCompleted()) {
            return consumer;
        }
        auto gate = makeResumeGate(consumer);
        promise.setWaitingConsumer(gate);
        if (!gate->arm()) {
            return consumer;
        }
        return handle_;
    }

    std::optional<T> await_resume() {
        if (!handle_) {
            return std::nullopt;
        }
        auto& promise = handle_.promise();
        if (promise.hasException()) {
            promise.rethrowIfException();
        }
        if (promise.hasValue()) {
            auto value = std::move(promise.value());
            promise.clearValue();
            return value;
        }
        return std::nullopt;
    }

 private:
    handle_type handle_ = nullptr;
};

}  // namespace detail

// 线程约束（C-08）：生产者与消费者必须由同一 executor/线程串行驱动。
// next() / co_yield / 完成 / 析构不得跨线程并发调用；跨线程场景需先经
// 事件投递（如 ownerThread_->postTask）串行化。若未来需要跨线程，
// 必须将 consumerGate_/consumerState_ 改为互斥或原子状态机。
template <typename T>
class [[nodiscard]] AsyncGenerator : public NonCopyable {
 public:
    using value_type = T;
    using promise_type = detail::AsyncGeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    AsyncGenerator() noexcept = default;

    explicit AsyncGenerator(handle_type handle) noexcept : handle_(handle) {}

    AsyncGenerator(AsyncGenerator&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    AsyncGenerator& operator=(AsyncGenerator&& other) noexcept {
        if (this != &other) {
            destroyFrame();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~AsyncGenerator() {
        destroyFrame();
    }

    [[nodiscard]] bool isValid() const noexcept {
        return handle_ != nullptr;
    }

    [[nodiscard]] bool isDone() const noexcept {
        return !handle_ || handle_.done();
    }

    [[nodiscard]] handle_type getHandle() const noexcept {
        return handle_;
    }

    auto next() & noexcept {
        return detail::AsyncGeneratorNextAwaiter<T>{handle_};
    }

    auto next() && noexcept {
        return detail::AsyncGeneratorNextAwaiter<T>{std::exchange(handle_, nullptr)};
    }

    template <typename Func>
    AsyncGenerator<std::invoke_result_t<Func, T>> transform(Func func);
    template <typename Pred>
    AsyncGenerator<T> filter(Pred pred);
    AsyncGenerator<T> take(int64_t count);
    AsyncGenerator<T> drop(int64_t count);
    template <typename Pred>
    AsyncGenerator<T> takeWhile(Pred pred);
    template <typename Pred>
    AsyncGenerator<T> dropWhile(Pred pred);
    AsyncGenerator<std::pair<int64_t, T>> enumerate();
    template <typename Init, typename BinaryOp>
    Task<Init> fold(Init init, BinaryOp op);
    Task<int64_t> count();
    template <typename Pred>
    Task<std::optional<T>> find(Pred pred);
    template <typename Pred>
    Task<bool> any(Pred pred);
    template <typename Pred>
    Task<bool> all(Pred pred);
    template <typename Func>
    Task<void> forEach(Func func);
    template <template <typename...> class Container = std::vector>
    Task<Container<T>> collect();

 private:
    void destroyFrame() noexcept {
        if (!handle_) {
            return;
        }
        auto handle = std::exchange(handle_, nullptr);
        if (handle.promise().frameControl()->destroy(handle)) {
            handle.destroy();
        }
    }

    handle_type handle_ = nullptr;
};

namespace detail {

template <typename T, typename Func>
AsyncGenerator<std::invoke_result_t<Func, T>> transformImpl(AsyncGenerator<T> gen, Func func) {
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        co_yield func(std::move(*v));
    }
}

template <typename T, typename Pred>
AsyncGenerator<T> filterImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        if (pred(*v)) {
            co_yield std::move(*v);
        }
    }
}

template <typename T>
AsyncGenerator<T> takeImpl(AsyncGenerator<T> gen, int64_t count) {
    int64_t n = 0;
    while (n < count) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        co_yield std::move(*v);
        ++n;
    }
}

template <typename T>
AsyncGenerator<T> dropImpl(AsyncGenerator<T> gen, int64_t count) {
    int64_t n = 0;
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        if (n < count) {
            ++n;
            continue;
        }
        co_yield std::move(*v);
    }
}

template <typename T, typename Pred>
AsyncGenerator<T> takeWhileImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value() || !pred(*v)) {
            break;
        }
        co_yield std::move(*v);
    }
}

template <typename T, typename Pred>
AsyncGenerator<T> dropWhileImpl(AsyncGenerator<T> gen, Pred pred) {
    bool d = true;
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        if (d && pred(*v)) {
            continue;
        }
        d = false;
        co_yield std::move(*v);
    }
}

template <typename T>
AsyncGenerator<std::pair<int64_t, T>> enumerateImpl(AsyncGenerator<T> gen) {
    int64_t i = 0;
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        co_yield std::make_pair(i++, std::move(*v));
    }
}

template <typename T, typename Init, typename BinaryOp>
Task<Init> foldImpl(AsyncGenerator<T> gen, Init init, BinaryOp op) {
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        init = op(std::move(init), std::move(*v));
    }
    co_return init;
}

template <typename T>
Task<int64_t> countImpl(AsyncGenerator<T> gen) {
    int64_t n = 0;
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        ++n;
    }
    co_return n;
}

template <typename T, typename Pred>
Task<std::optional<T>> findImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        if (pred(*v)) {
            co_return std::move(v);
        }
    }
    co_return std::nullopt;
}

template <typename T, typename Pred>
Task<bool> anyImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        if (pred(*v)) {
            co_return true;
        }
    }
    co_return false;
}

template <typename T, typename Pred>
Task<bool> allImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        if (!pred(*v)) {
            co_return false;
        }
    }
    co_return true;
}

template <typename T, typename Func>
Task<void> forEachImpl(AsyncGenerator<T> gen, Func func) {
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        func(std::move(*v));
    }
    co_return;
}

template <typename T, template <typename...> class C>
Task<C<T>> collectImpl(AsyncGenerator<T> gen) {
    C<T> r;
    while (true) {
        auto v = co_await gen.next();
        if (!v.has_value()) {
            break;
        }
        if constexpr (requires { r.push_back(std::move(*v)); }) {
            r.push_back(std::move(*v));
        } else if constexpr (requires { r.insert(std::move(*v)); }) {
            r.insert(std::move(*v));
        }
    }
    co_return r;
}

}  // namespace detail

template <typename T>
template <typename Func>
AsyncGenerator<std::invoke_result_t<Func, T>> AsyncGenerator<T>::transform(Func func) {
    return detail::transformImpl(std::move(*this), std::move(func));
}

template <typename T>
template <typename Pred>
AsyncGenerator<T> AsyncGenerator<T>::filter(Pred pred) {
    return detail::filterImpl(std::move(*this), std::move(pred));
}

template <typename T>
AsyncGenerator<T> AsyncGenerator<T>::take(int64_t c) {
    return detail::takeImpl(std::move(*this), c);
}

template <typename T>
AsyncGenerator<T> AsyncGenerator<T>::drop(int64_t c) {
    return detail::dropImpl(std::move(*this), c);
}

template <typename T>
template <typename Pred>
AsyncGenerator<T> AsyncGenerator<T>::takeWhile(Pred pred) {
    return detail::takeWhileImpl(std::move(*this), std::move(pred));
}

template <typename T>
template <typename Pred>
AsyncGenerator<T> AsyncGenerator<T>::dropWhile(Pred pred) {
    return detail::dropWhileImpl(std::move(*this), std::move(pred));
}

template <typename T>
AsyncGenerator<std::pair<int64_t, T>> AsyncGenerator<T>::enumerate() {
    return detail::enumerateImpl(std::move(*this));
}

template <typename T>
template <typename Init, typename BinaryOp>
Task<Init> AsyncGenerator<T>::fold(Init init, BinaryOp op) {
    return detail::foldImpl(std::move(*this), std::move(init), std::move(op));
}

template <typename T>
Task<int64_t> AsyncGenerator<T>::count() {
    return detail::countImpl(std::move(*this));
}

template <typename T>
template <typename Pred>
Task<std::optional<T>> AsyncGenerator<T>::find(Pred p) {
    return detail::findImpl(std::move(*this), std::move(p));
}

template <typename T>
template <typename Pred>
Task<bool> AsyncGenerator<T>::any(Pred p) {
    return detail::anyImpl(std::move(*this), std::move(p));
}

template <typename T>
template <typename Pred>
Task<bool> AsyncGenerator<T>::all(Pred p) {
    return detail::allImpl(std::move(*this), std::move(p));
}

template <typename T>
template <typename Func>
Task<void> AsyncGenerator<T>::forEach(Func f) {
    return detail::forEachImpl(std::move(*this), std::move(f));
}

template <typename T>
template <template <typename...> class C>
Task<C<T>> AsyncGenerator<T>::collect() {
    return detail::collectImpl<T, C>(std::move(*this));
}

namespace detail {

template <typename T>
AsyncGenerator<T> AsyncGeneratorPromise<T>::get_return_object() noexcept {
    return AsyncGenerator<T>{std::coroutine_handle<AsyncGeneratorPromise<T>>::from_promise(*this)};
}

}  // namespace detail

}  // namespace slark
