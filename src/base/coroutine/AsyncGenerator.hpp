// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <cstdint>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "NonCopyable.h"
#include "Task.hpp"

namespace slark {

template <typename T>
class AsyncGenerator;

enum class GeneratorStatus : uint8_t { Running, Completed, Closed, Failed };
enum class GeneratorPhase : uint8_t {
    InitialSuspended,
    Executing,
    WaitingConsumer,
    WaitingExternal,
    Yielded,
    FinalSuspended,
};

namespace detail {

enum class ConsumerState : uint8_t { None, Waiting };
using GeneratorContinuation = ContinuationLink;

template <typename T>
class GeneratorState final {
 public:
    explicit GeneratorState(
        std::coroutine_handle<> producer,
        std::shared_ptr<CoroutineFrameControl> frameControl) noexcept
        : producer_(producer), frameControl_(std::move(frameControl)) {}

    [[nodiscard]] std::coroutine_handle<> producer() const noexcept {
        return producer_;
    }

    void setProducer(std::coroutine_handle<> producer) noexcept {
        producer_ = producer;
    }

    void clearProducer() noexcept {
        producer_ = nullptr;
    }

    [[nodiscard]] std::shared_ptr<CoroutineFrameControl> frameControl() const noexcept {
        return frameControl_;
    }

    [[nodiscard]] GeneratorStatus status() const noexcept {
        return status_.load(std::memory_order_acquire);
    }

    bool transition(GeneratorStatus expected, GeneratorStatus desired) noexcept {
        return status_.compare_exchange_strong(
            expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void setPhase(GeneratorPhase phase) noexcept {
        phase_ = phase;
    }

    [[nodiscard]] GeneratorPhase phase() const noexcept {
        return phase_;
    }

    void setFailure(std::exception_ptr failure) noexcept {
        failure_ = std::move(failure);
    }

    [[nodiscard]] bool hasFailure() const noexcept {
        return failure_ != nullptr;
    }

    void rethrowFailure() const {
        if (failure_) {
            std::rethrow_exception(failure_);
        }
    }

    bool setWaitingConsumer(std::coroutine_handle<> consumer,
                            std::shared_ptr<CoroutineFrameControl> frameControl,
                            ExecutionLease* targetLeaseSlot,
                            ExecutionLease* sourceLeaseSlot,
                            uint64_t* generation) noexcept {
        if (!sourceLeaseSlot) {
            consumerState_ = ConsumerState::None;
            return false;
        }
        consumerContinuation_ = ContinuationLink::registerTarget(
            consumer, std::move(frameControl), targetLeaseSlot, sourceLeaseSlot);
        if (!consumerContinuation_.isRegistered()) {
            consumerState_ = ConsumerState::None;
            return false;
        }
        ++consumerGeneration_;
        if (generation) {
            *generation = consumerGeneration_;
        }
        consumerState_ = ConsumerState::Waiting;
        phase_ = GeneratorPhase::WaitingConsumer;
        return true;
    }

    std::coroutine_handle<> beginConsumerTransfer(
        std::coroutine_handle<> producer) noexcept {
        if (consumerState_ != ConsumerState::Waiting ||
            !consumerContinuation_.isRegistered() || !consumerContinuation_.sourceLeaseSlot) {
            return nullptr;
        }
        auto consumer = std::move(consumerContinuation_);
        if (!consumer.acquireTarget()) {
            consumerState_ = ConsumerState::None;
            return nullptr;
        }
        auto transferLease = frameControl_->tryAcquire(producer);
        if (!transferLease) {
            consumer.reset();
            consumerState_ = ConsumerState::None;
            return nullptr;
        }
        *consumer.targetLeaseSlot = std::move(consumer.lease);
        *consumer.sourceLeaseSlot = std::move(transferLease);
        consumer.sourceLeaseSlot = nullptr;
        consumerState_ = ConsumerState::None;
        phase_ = GeneratorPhase::Yielded;
        auto target = consumer.handle;
        consumer.handle = nullptr;
        consumer.control.reset();
        return target;
    }

    void cancelWaitingConsumer(std::coroutine_handle<> consumer, uint64_t generation) noexcept {
        if (consumerState_ == ConsumerState::Waiting &&
            consumerGeneration_ == generation && consumerContinuation_.handle.address() == consumer.address()) {
            releaseConsumerContinuation();
        }
    }

    void releaseConsumerContinuation() noexcept {
        consumerContinuation_.reset();
        consumerState_ = ConsumerState::None;
    }

    void setExternalGate(std::shared_ptr<ResumeGate> gate) noexcept {
        if (status() == GeneratorStatus::Closed) {
            if (gate) {
                gate->disarm();
            }
            return;
        }
        auto previous = std::atomic_exchange_explicit(
            &externalGate_, std::move(gate), std::memory_order_acq_rel);
        if (previous) {
            previous->disarm();
        }
    }

    void disarmExternalGate() noexcept {
        auto gate = std::atomic_exchange_explicit(
            &externalGate_, std::shared_ptr<ResumeGate>{}, std::memory_order_acq_rel);
        if (gate) {
            gate->disarm();
        }
    }

    void close() noexcept {
        const auto currentStatus = status();
        if (currentStatus == GeneratorStatus::Closed) {
            return;
        }
        disarmExternalGate();

        auto producer = producer_;
        bool destroyNow = false;
        if (producer) {
            // First synchronize with a signal already entering ResumeGate::Resuming, then update state.
            destroyNow = frameControl_->destroy(producer);
        }

        if (currentStatus == GeneratorStatus::Running) {
            transition(GeneratorStatus::Running, GeneratorStatus::Closed);
        }
        disarmExternalGate();
        phase_ = GeneratorPhase::FinalSuspended;

        auto consumer = std::move(consumerContinuation_);
        consumerState_ = ConsumerState::None;
        producer_ = nullptr;
        if (destroyNow) {
            producer.destroy();
        }

        if (!consumer.isRegistered() || !consumer.acquireTarget()) {
            consumer.reset();
            return;
        }
        *consumer.targetLeaseSlot = std::move(consumer.lease);
        auto target = consumer.handle;
        auto targetControl = std::move(consumer.control);
        consumer.handle = nullptr;
        consumer.targetLeaseSlot = nullptr;
        consumer.sourceLeaseSlot = nullptr;
        targetControl->resume(target);
    }

 private:
    std::coroutine_handle<> producer_;
    std::shared_ptr<CoroutineFrameControl> frameControl_;
    std::atomic<GeneratorStatus> status_{GeneratorStatus::Running};
    GeneratorPhase phase_{GeneratorPhase::InitialSuspended};
    std::exception_ptr failure_;
    ContinuationLink consumerContinuation_;
    std::shared_ptr<ResumeGate> externalGate_;
    uint64_t consumerGeneration_{0};
    ConsumerState consumerState_ = ConsumerState::None;
};

template <typename T>
class PendingConsumerRegistration final {
 public:
    using state_type = GeneratorState<T>;

    PendingConsumerRegistration() noexcept = default;

    PendingConsumerRegistration(const PendingConsumerRegistration&) = delete;
    PendingConsumerRegistration& operator=(const PendingConsumerRegistration&) = delete;

    PendingConsumerRegistration(PendingConsumerRegistration&& other) noexcept
        : state_(std::move(other.state_)),
          consumer_(std::exchange(other.consumer_, nullptr)),
          generation_(std::exchange(other.generation_, 0)) {}

    PendingConsumerRegistration& operator=(PendingConsumerRegistration&& other) noexcept {
        if (this != &other) {
            reset();
            state_ = std::move(other.state_);
            consumer_ = std::exchange(other.consumer_, nullptr);
            generation_ = std::exchange(other.generation_, 0);
        }
        return *this;
    }

    ~PendingConsumerRegistration() {
        reset();
    }

    void assign(const std::shared_ptr<state_type>& state,
                std::coroutine_handle<> consumer,
                uint64_t generation) noexcept {
        reset();
        state_ = state;
        consumer_ = consumer;
        generation_ = generation;
    }

    void reset() noexcept {
        if (auto state = state_.lock()) {
            state->cancelWaitingConsumer(consumer_, generation_);
        }
        state_.reset();
        consumer_ = nullptr;
        generation_ = 0;
    }

 private:
    std::weak_ptr<state_type> state_;
    std::coroutine_handle<> consumer_;
    uint64_t generation_{0};
};

template <typename Promise>
class AsyncGeneratorYieldAwaiter {
 public:
    explicit AsyncGeneratorYieldAwaiter(Promise& promise) noexcept : promise_(promise) {}

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> producer) noexcept {
        releaseTransferLeases(producer.promise());
        auto consumer = promise_.beginConsumerTransfer(producer);
        if (!consumer) {
            promise_.releaseConsumerContinuation();
            return std::noop_coroutine();
        }
        return consumer;
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
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> producer) noexcept {
        auto& promise = producer.promise();
        promise.setCompleted();
        releaseTransferLeases(promise);
        auto consumer = promise.beginConsumerTransfer(producer);
        if (!consumer) {
            promise.releaseConsumerContinuation();
            return std::noop_coroutine();
        }
        return consumer;
    }

    void await_resume() noexcept {}
};

template <typename T>
class AsyncGeneratorPromise : public PromiseCancellation {
public:
    using value_type = T;
    using reference_type = T&;
    using state_type = GeneratorState<T>;

    AsyncGeneratorPromise()
        : state_(std::make_shared<state_type>(std::coroutine_handle<>{}, frameControl_)) {}

    FrameStartAwaiter<AsyncGeneratorPromise<T>> initial_suspend() noexcept {
        return {this};
    }

    AsyncGeneratorFinalAwaiter<T> final_suspend() noexcept {
        return {};
    }

    void unhandled_exception() noexcept {
        if (state_->status() == GeneratorStatus::Running) {
            state_->setFailure(std::current_exception());
            if (state_->transition(GeneratorStatus::Running, GeneratorStatus::Failed)) {
                state_->setPhase(GeneratorPhase::FinalSuspended);
            }
        }
    }

    void return_void() noexcept {
        setCompleted();
    }

    AsyncGeneratorYieldAwaiter<AsyncGeneratorPromise<T>> yield_value(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>) {
        value_.emplace(std::move(value));
        state_->setPhase(GeneratorPhase::Yielded);
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

    void rethrowIfException() const {
        state_->rethrowFailure();
    }

    bool hasException() const noexcept {
        return state_->hasFailure();
    }

    bool isCompleted() const noexcept {
        const auto status = state_->status();
        return status == GeneratorStatus::Completed || status == GeneratorStatus::Closed ||
               status == GeneratorStatus::Failed;
    }

    GeneratorStatus status() const noexcept {
        return state_->status();
    }

    void setCompleted() noexcept {
        state_->transition(GeneratorStatus::Running, GeneratorStatus::Completed);
        state_->setPhase(GeneratorPhase::FinalSuspended);
    }

    void markWaitingExternal() noexcept {
        state_->setPhase(GeneratorPhase::WaitingExternal);
    }

    void markExecuting() noexcept {
        state_->setPhase(GeneratorPhase::Executing);
    }

    [[nodiscard]] std::shared_ptr<state_type> state() const noexcept {
        return state_;
    }

    void initializeState(std::coroutine_handle<> producer) noexcept {
        state_->setProducer(producer);
    }

    bool setWaitingConsumer(std::coroutine_handle<> consumer,
                            std::shared_ptr<CoroutineFrameControl> frameControl,
                            ExecutionLease* targetLeaseSlot,
                            ExecutionLease* sourceLeaseSlot,
                            uint64_t* generation) noexcept {
        return state_->setWaitingConsumer(
            consumer, std::move(frameControl), targetLeaseSlot, sourceLeaseSlot, generation);
    }

    std::coroutine_handle<> beginConsumerTransfer(
        std::coroutine_handle<> producer) noexcept {
        return state_->beginConsumerTransfer(producer);
    }

    void cancelWaitingConsumer(std::coroutine_handle<> consumer, uint64_t generation) noexcept {
        state_->cancelWaitingConsumer(consumer, generation);
    }

    void releaseConsumerContinuation() noexcept {
        state_->releaseConsumerContinuation();
    }

    void releaseIncomingTransferLease() noexcept {
        incomingTransferLease_.reset();
    }

    void close() noexcept {
        state_->close();
    }

    void setExternalGate(std::shared_ptr<ResumeGate> gate) noexcept {
        state_->setExternalGate(std::move(gate));
    }

    void releaseContinuationLease() noexcept {
        continuationLease_.reset();
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

    // Execution domain this generator's producer must run on. Null keeps the historical
    // behaviour (resume on the notifying thread).
    [[nodiscard]] const std::shared_ptr<IExecutor>& executor() const noexcept {
        return executor_;
    }

    void setExecutor(std::shared_ptr<IExecutor> executor) noexcept {
        executor_ = std::move(executor);
    }

 private:
    std::optional<T> value_;
    ExecutionLease continuationLease_;
    ExecutionLease incomingTransferLease_;
    std::shared_ptr<CoroutineFrameControl> frameControl_ =
        std::make_shared<CoroutineFrameControl>();
    std::shared_ptr<state_type> state_;
    std::shared_ptr<IExecutor> executor_;
};

template <typename T>
class AsyncGeneratorNextAwaiter {
 public:
    using promise_type = AsyncGeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using state_type = GeneratorState<T>;

    AsyncGeneratorNextAwaiter() noexcept = default;

    explicit AsyncGeneratorNextAwaiter(std::shared_ptr<state_type> state,
                                       bool ownsState = false) noexcept
        : state_(std::move(state)), ownsState_(ownsState) {}

    AsyncGeneratorNextAwaiter(AsyncGeneratorNextAwaiter&& other) noexcept
        : state_(std::move(other.state_)),
          ownsState_(std::exchange(other.ownsState_, false)),
          registration_(std::move(other.registration_)) {}

    AsyncGeneratorNextAwaiter& operator=(AsyncGeneratorNextAwaiter&& other) noexcept {
        if (this != &other) {
            registration_.reset();
            closeOwnedState();
            state_ = std::move(other.state_);
            ownsState_ = std::exchange(other.ownsState_, false);
            registration_ = std::move(other.registration_);
        }
        return *this;
    }

    ~AsyncGeneratorNextAwaiter() {
        registration_.reset();
        closeOwnedState();
    }

    bool await_ready() noexcept {
        if (!state_ || state_->status() != GeneratorStatus::Running) {
            return true;
        }
        auto producer = state_->producer();
        if (!producer) {
            return true;
        }
        return std::coroutine_handle<promise_type>::from_address(producer.address())
            .promise()
            .hasValue();
    }

    template <typename ConsumerPromise>
        requires ContinuationPromise<ConsumerPromise>
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<ConsumerPromise> consumer) noexcept {
        releaseTransferLeases(consumer.promise());
        if (!state_ || state_->status() != GeneratorStatus::Running) {
            return consumer;
        }
        auto producer = state_->producer();
        if (!producer) {
            return consumer;
        }
        auto handle = std::coroutine_handle<promise_type>::from_address(producer.address());
        auto& promise = handle.promise();
        if (promise.hasValue() || promise.isCompleted()) {
            return consumer;
        }

        // Local/explicit domain wins; the consumer's domain is only a fallback.
        if constexpr (requires { consumer.promise().executor(); }) {
            if (promise.executor() == nullptr) {
                promise.setExecutor(consumer.promise().executor());
            }
        }
        if constexpr (requires { consumer.promise().cancelToken(); }) {
            const auto* token = consumer.promise().cancelToken();
            if (token && !promise.cancelToken()) {
                promise.setCancelToken(*token);
            }
        }

        uint64_t generation = 0;
        if (!promise.setWaitingConsumer(
                consumer, consumer.promise().frameControl(),
                consumer.promise().continuationLeaseSlot(),
                consumer.promise().incomingTransferLeaseSlot(), &generation)) {
            return consumer;
        }
        promise.markExecuting();
        registration_.assign(state_, consumer, generation);
        return producer;
    }

    std::optional<T> await_resume() {
        auto state = state_;
        registration_.reset();
        if (!state) {
            return std::nullopt;
        }
        if (state->status() == GeneratorStatus::Failed) {
            state->rethrowFailure();
        }
        auto producer = state->producer();
        if (!producer) {
            return std::nullopt;
        }
        auto handle = std::coroutine_handle<promise_type>::from_address(producer.address());
        auto& promise = handle.promise();
        std::optional<T> value;
        if (promise.hasValue()) {
            value.emplace(std::move(promise.value()));
            promise.clearValue();
        }
        promise.releaseConsumerContinuation();
        return value;
    }

 private:
    void closeOwnedState() noexcept {
        if (ownsState_ && state_) {
            state_->close();
            ownsState_ = false;
        }
    }

    std::shared_ptr<state_type> state_;
    bool ownsState_ = false;
    PendingConsumerRegistration<T> registration_;
};

}  // namespace detail

// Thread constraint (C-08): producer and consumer must be driven serially by the same
// executor/thread. next() / co_yield / completion / destruction must not be called
// concurrently across threads; cross-thread scenarios must be serialized via event
// dispatch (e.g. ownerThread_->postTask). The continuation control block only rejects
// stale consumers; it does not make generator state concurrent-safe.
template <typename T>
class [[nodiscard]] AsyncGenerator : public NonCopyable {
 public:
    using value_type = T;
    using promise_type = detail::AsyncGeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using state_type = detail::GeneratorState<T>;

    AsyncGenerator() noexcept = default;

    explicit AsyncGenerator(std::shared_ptr<state_type> state) noexcept : state_(std::move(state)) {}

    AsyncGenerator(AsyncGenerator&& other) noexcept
        : state_(std::move(other.state_)) {}

    AsyncGenerator& operator=(AsyncGenerator&& other) noexcept {
        if (this != &other) {
            close();
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~AsyncGenerator() {
        close();
    }

    [[nodiscard]] bool isValid() const noexcept {
        return state_ && state_->producer();
    }

    [[nodiscard]] bool isDone() const noexcept {
        return !state_ || !state_->producer() ||
               state_->status() != GeneratorStatus::Running;
    }

    [[nodiscard]] GeneratorStatus status() const noexcept {
        return state_ ? state_->status() : GeneratorStatus::Closed;
    }

    [[nodiscard]] GeneratorPhase phase() const noexcept {
        return state_ ? state_->phase() : GeneratorPhase::FinalSuspended;
    }

    [[nodiscard]] handle_type getHandle() const noexcept {
        if (!state_ || !state_->producer()) {
            return nullptr;
        }
        return handle_type::from_address(state_->producer().address());
    }

    void close() noexcept {
        if (state_) {
            state_->close();
        }
    }

    // Binds the producer to an execution domain before its first next(). A closed generator
    // has no producer left to bind, so the call does nothing there.
    void setExecutor(std::shared_ptr<IExecutor> executor) {
        if (state_ == nullptr) {
            return;
        }
        if (auto producer = state_->producer()) {
            auto& promise = handle_type::from_address(producer.address()).promise();
            detail::requireNotStarted(promise, "AsyncGenerator::setExecutor after the first next()");
            promise.setExecutor(std::move(executor));
        }
    }

    auto next() & noexcept {
        return detail::AsyncGeneratorNextAwaiter<T>{state_};
    }

    auto next() && noexcept {
        return detail::AsyncGeneratorNextAwaiter<T>{std::move(state_), true};
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
    std::shared_ptr<state_type> state_;
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
    auto handle = std::coroutine_handle<AsyncGeneratorPromise<T>>::from_promise(*this);
    initializeState(handle);
    return AsyncGenerator<T>{state_};
}

}  // namespace detail

}  // namespace slark
