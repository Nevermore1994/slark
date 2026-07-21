//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "Coroutine.hpp"
#include "NonCopyable.h"

#include <atomic>
#include <coroutine>
#include <exception>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

namespace slark {

template <typename T>
class AsyncGenerator;

namespace detail {

template <typename T>
class AsyncGeneratorPromise;

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
            consumer.resume();
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
            consumer.resume();
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

    AsyncGeneratorPromise() noexcept = default;

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

    slark::AsyncGenerator<T> get_return_object() noexcept;

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
        return completed.load(std::memory_order_acquire);
    }

    void setCompleted() noexcept {
        completed.store(true, std::memory_order_release);
        if (auto control = control_.lock()) {
            control->markCompleted();
        }
    }

    ~AsyncGeneratorPromise() noexcept = default;

    void setWaitingConsumer(std::coroutine_handle<> consumer,
                            std::shared_ptr<CoroutineControlBlock> consumerControl) noexcept {
        consumerHandle_ = consumer;
        consumerControl_ = consumerControl;
        consumerState_ = ConsumerState::Waiting;
    }

    std::coroutine_handle<> takeWaitingConsumer() noexcept {
        if (consumerState_ == ConsumerState::Waiting) {
            consumerState_ = ConsumerState::Resumed;
            auto h = std::exchange(consumerHandle_, nullptr);
            // 检查 consumer frame 是否仍存活
            if (auto control = consumerControl_.lock()) {
                (void)control;
            } else {
                return nullptr;
            }
            return h;
        }
        return nullptr;
    }

    template <typename Awaitable>
    auto await_transform(Awaitable&& awaitable) noexcept(
        noexcept(std::forward<Awaitable>(awaitable))) {
        return std::forward<Awaitable>(awaitable);
    }

private:
    std::optional<T> value_;
    std::exception_ptr exception_;
    std::weak_ptr<CoroutineControlBlock> control_;
    std::coroutine_handle<> consumerHandle_ = nullptr;
    std::weak_ptr<CoroutineControlBlock> consumerControl_;
    ConsumerState consumerState_ = ConsumerState::None;

public:
    std::atomic<bool> completed{false};
};

template <typename T>
class AsyncGeneratorNextAwaiter {
public:
    using promise_type = AsyncGeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    AsyncGeneratorNextAwaiter(std::shared_ptr<CoroutineControlBlock> control,
                              handle_type handle) noexcept
        : control_(std::move(control)), handle_(handle) {}

    bool await_ready() noexcept {
        if (!control_) {
            return true;
        }
        auto& promise = handle_.promise();
        return promise.hasValue() || promise.isCompleted();
    }

    template <typename ConsumerPromise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<ConsumerPromise> consumer) noexcept {
        auto& promise = handle_.promise();
        if (promise.hasValue() || promise.isCompleted()) {
            return consumer;
        }
        auto consumerControl = consumer.promise().makeResumeHandle();
        auto consumerControlBlock = consumerControl ? consumerControl->controlBlock() : nullptr;
        promise.setWaitingConsumer(consumer, std::move(consumerControlBlock));
        return handle_;
    }

    std::optional<T> await_resume() {
        if (!control_) {
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
    std::shared_ptr<CoroutineControlBlock> control_;
    handle_type handle_ = nullptr;
};

} // namespace detail

template <typename T>
class [[nodiscard]] AsyncGenerator : public NonCopyable {
public:
    using value_type = T;
    using promise_type = detail::AsyncGeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit AsyncGenerator(handle_type handle) noexcept : handle_(handle) {
        if (handle_) {
            control_ = std::make_shared<CoroutineControlBlock>(handle_);
            handle_.promise().setControl(control_);
        }
    }

    // nullptr_t 构造函数：创建一个空的（已释放的）AsyncGenerator，用于赋值释放
    /* implicit */ AsyncGenerator(std::nullptr_t) noexcept {} // NOLINT

    AsyncGenerator(AsyncGenerator&& other) noexcept
        : control_(std::move(other.control_)), handle_(std::exchange(other.handle_, nullptr)) {}

    AsyncGenerator& operator=(AsyncGenerator&& other) noexcept {
        if (this != &other) {
            control_ = std::move(other.control_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~AsyncGenerator() = default;

    bool isValid() const noexcept {
        return handle_ != nullptr;
    }

    bool isDone() const noexcept {
        return !control_ || control_->isDone();
    }

    handle_type getHandle() const noexcept {
        return handle_;
    }

    auto next() & noexcept {
        return detail::AsyncGeneratorNextAwaiter<T>{control_, handle_};
    }

    auto next() && noexcept {
        return detail::AsyncGeneratorNextAwaiter<T>{std::move(control_),
                                                    std::exchange(handle_, nullptr)};
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
    Coroutine<Init> fold(Init init, BinaryOp op);

    Coroutine<int64_t> count();

    template <typename Pred>
    Coroutine<std::optional<T>> find(Pred pred);

    template <typename Pred>
    Coroutine<bool> any(Pred pred);

    template <typename Pred>
    Coroutine<bool> all(Pred pred);

    template <typename Func>
    Coroutine<void> forEach(Func func);

    template <template <typename...> class Container = std::vector>
    Coroutine<Container<T>> collect();

private:
    std::shared_ptr<CoroutineControlBlock> control_;
    handle_type handle_ = nullptr;
};

namespace detail {

template <typename T, typename Func>
AsyncGenerator<std::invoke_result_t<Func, T>> transformImpl(AsyncGenerator<T> gen, Func func) {
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        co_yield func(std::move(*value));
    }
}

template <typename T, typename Pred>
AsyncGenerator<T> filterImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        if (pred(*value)) {
            co_yield std::move(*value);
        }
    }
}

template <typename T>
AsyncGenerator<T> takeImpl(AsyncGenerator<T> gen, int64_t count) {
    int64_t taken = 0;
    while (taken < count) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        co_yield std::move(*value);
        ++taken;
    }
}

template <typename T>
AsyncGenerator<T> dropImpl(AsyncGenerator<T> gen, int64_t count) {
    int64_t dropped = 0;
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        if (dropped < count) {
            ++dropped;
            continue;
        }
        co_yield std::move(*value);
    }
}

template <typename T, typename Pred>
AsyncGenerator<T> takeWhileImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        if (!pred(*value)) {
            break;
        }
        co_yield std::move(*value);
    }
}

template <typename T, typename Pred>
AsyncGenerator<T> dropWhileImpl(AsyncGenerator<T> gen, Pred pred) {
    bool dropping = true;
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        if (dropping && pred(*value)) {
            continue;
        }
        dropping = false;
        co_yield std::move(*value);
    }
}

template <typename T>
AsyncGenerator<std::pair<int64_t, T>> enumerateImpl(AsyncGenerator<T> gen) {
    int64_t index = 0;
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        co_yield std::make_pair(index++, std::move(*value));
    }
}

template <typename T, typename Init, typename BinaryOp>
Coroutine<Init> foldImpl(AsyncGenerator<T> gen, Init init, BinaryOp op) {
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        init = op(std::move(init), std::move(*value));
    }
    co_return init;
}

template <typename T>
Coroutine<int64_t> countImpl(AsyncGenerator<T> gen) {
    int64_t n = 0;
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        ++n;
    }
    co_return n;
}

template <typename T, typename Pred>
Coroutine<std::optional<T>> findImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        if (pred(*value)) {
            co_return std::move(value);
        }
    }
    co_return std::nullopt;
}

template <typename T, typename Pred>
Coroutine<bool> anyImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        if (pred(*value)) {
            co_return true;
        }
    }
    co_return false;
}

template <typename T, typename Pred>
Coroutine<bool> allImpl(AsyncGenerator<T> gen, Pred pred) {
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        if (!pred(*value)) {
            co_return false;
        }
    }
    co_return true;
}

template <typename T, typename Func>
Coroutine<void> forEachImpl(AsyncGenerator<T> gen, Func func) {
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        func(std::move(*value));
    }
}

template <typename T, template <typename...> class Container>
Coroutine<Container<T>> collectImpl(AsyncGenerator<T> gen) {
    Container<T> result;
    while (true) {
        auto value = co_await gen.next();
        if (!value.has_value()) {
            break;
        }
        if constexpr (requires { result.push_back(std::move(*value)); }) {
            result.push_back(std::move(*value));
        } else if constexpr (requires { result.insert(std::move(*value)); }) {
            result.insert(std::move(*value));
        }
    }
    co_return result;
}

} // namespace detail

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
AsyncGenerator<T> AsyncGenerator<T>::take(int64_t count) {
    return detail::takeImpl(std::move(*this), count);
}

template <typename T>
AsyncGenerator<T> AsyncGenerator<T>::drop(int64_t count) {
    return detail::dropImpl(std::move(*this), count);
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
Coroutine<Init> AsyncGenerator<T>::fold(Init init, BinaryOp op) {
    return detail::foldImpl(std::move(*this), std::move(init), std::move(op));
}

template <typename T>
Coroutine<int64_t> AsyncGenerator<T>::count() {
    return detail::countImpl(std::move(*this));
}

template <typename T>
template <typename Pred>
Coroutine<std::optional<T>> AsyncGenerator<T>::find(Pred pred) {
    return detail::findImpl(std::move(*this), std::move(pred));
}

template <typename T>
template <typename Pred>
Coroutine<bool> AsyncGenerator<T>::any(Pred pred) {
    return detail::anyImpl(std::move(*this), std::move(pred));
}

template <typename T>
template <typename Pred>
Coroutine<bool> AsyncGenerator<T>::all(Pred pred) {
    return detail::allImpl(std::move(*this), std::move(pred));
}

template <typename T>
template <typename Func>
Coroutine<void> AsyncGenerator<T>::forEach(Func func) {
    return detail::forEachImpl(std::move(*this), std::move(func));
}

template <typename T>
template <template <typename...> class Container>
Coroutine<Container<T>> AsyncGenerator<T>::collect() {
    return detail::collectImpl<T, Container>(std::move(*this));
}

namespace detail {

template <typename Func>
struct TransformAdapter {
    Func func;
};

template <typename Pred>
struct FilterAdapter {
    Pred pred;
};

struct TakeAdapter {
    int64_t count;
};

struct DropAdapter {
    int64_t count;
};

template <typename Pred>
struct TakeWhileAdapter {
    Pred pred;
};

template <typename Pred>
struct DropWhileAdapter {
    Pred pred;
};

struct EnumerateAdapter {};

} // namespace detail

template <typename T, typename Func>
auto operator|(AsyncGenerator<T>&& gen, detail::TransformAdapter<Func> adapter) {
    return detail::transformImpl(std::move(gen), std::move(adapter.func));
}

template <typename T, typename Pred>
auto operator|(AsyncGenerator<T>&& gen, detail::FilterAdapter<Pred> adapter) {
    return detail::filterImpl(std::move(gen), std::move(adapter.pred));
}

template <typename T>
auto operator|(AsyncGenerator<T>&& gen, detail::TakeAdapter adapter) {
    return detail::takeImpl(std::move(gen), adapter.count);
}

template <typename T>
auto operator|(AsyncGenerator<T>&& gen, detail::DropAdapter adapter) {
    return detail::dropImpl(std::move(gen), adapter.count);
}

template <typename T, typename Pred>
auto operator|(AsyncGenerator<T>&& gen, detail::TakeWhileAdapter<Pred> adapter) {
    return detail::takeWhileImpl(std::move(gen), std::move(adapter.pred));
}

template <typename T, typename Pred>
auto operator|(AsyncGenerator<T>&& gen, detail::DropWhileAdapter<Pred> adapter) {
    return detail::dropWhileImpl(std::move(gen), std::move(adapter.pred));
}

template <typename T>
auto operator|(AsyncGenerator<T>&& gen, detail::EnumerateAdapter) {
    return detail::enumerateImpl(std::move(gen));
}

template <typename Func>
auto transform(Func func) {
    return detail::TransformAdapter<Func>{std::move(func)};
}

template <typename Pred>
auto filter(Pred pred) {
    return detail::FilterAdapter<Pred>{std::move(pred)};
}

inline auto take(int64_t count) {
    return detail::TakeAdapter{count};
}

inline auto drop(int64_t count) {
    return detail::DropAdapter{count};
}

template <typename Pred>
auto takeWhile(Pred pred) {
    return detail::TakeWhileAdapter<Pred>{std::move(pred)};
}

template <typename Pred>
auto dropWhile(Pred pred) {
    return detail::DropWhileAdapter<Pred>{std::move(pred)};
}

inline auto enumerate() {
    return detail::EnumerateAdapter{};
}

namespace detail {

template <typename T>
slark::AsyncGenerator<T> AsyncGeneratorPromise<T>::get_return_object() noexcept {
    return slark::AsyncGenerator<T>{
        std::coroutine_handle<AsyncGeneratorPromise<T>>::from_promise(*this)};
}

} // namespace detail

} // namespace slark
