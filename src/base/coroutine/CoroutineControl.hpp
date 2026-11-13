// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "CancellationToken.h"
#include "CoroutineScheduler.hpp"
#include "Executor.h"
#include "Log.hpp"

namespace slark {

class CoroutineFrameControl;

class ExecutionLease final {
 public:
    ExecutionLease() noexcept = default;

    ExecutionLease(const ExecutionLease&) = delete;
    ExecutionLease& operator=(const ExecutionLease&) = delete;

    ExecutionLease(ExecutionLease&& other) noexcept
        : control_(std::exchange(other.control_, nullptr)) {}

    ExecutionLease& operator=(ExecutionLease&& other) noexcept {
        if (this != &other) {
            reset();
            control_ = std::exchange(other.control_, nullptr);
        }
        return *this;
    }

    ~ExecutionLease() {
        reset();
    }

    [[nodiscard]] bool isValid() const noexcept {
        return control_ != nullptr;
    }

    explicit operator bool() const noexcept {
        return isValid();
    }

    void reset() noexcept;

 private:
    explicit ExecutionLease(CoroutineFrameControl* control) noexcept : control_(control) {}

    CoroutineFrameControl* control_ = nullptr;

    friend class CoroutineFrameControl;
};

namespace detail {

inline thread_local uint32_t coroutineResumeDepth = 0;
inline thread_local std::vector<std::coroutine_handle<>> deferredCoroutineDestroys;

inline void deferCoroutineDestroy(std::coroutine_handle<> handle) noexcept {
    if (!handle) {
        return;
    }
    if (coroutineResumeDepth == 0) {
        handle.destroy();
        return;
    }
    try {
        deferredCoroutineDestroys.push_back(handle);
    } catch (...) {
        std::terminate();
    }
}

inline void drainDeferredCoroutineDestroys() noexcept {
    if (coroutineResumeDepth != 0) {
        return;
    }
    while (!deferredCoroutineDestroys.empty()) {
        auto handle = deferredCoroutineDestroys.back();
        deferredCoroutineDestroys.pop_back();
        if (handle) {
            handle.destroy();
        }
    }
}

}  // namespace detail

class CoroutineFrameControl final {
 public:
    CoroutineFrameControl() = default;

    // Tracked frames are done only after finish(); handle.done() turns true too early.
    explicit CoroutineFrameControl(bool tracksFinalSuspend) noexcept
        : tracksFinalSuspend_(tracksFinalSuspend) {}

    CoroutineFrameControl(const CoroutineFrameControl&) = delete;
    CoroutineFrameControl& operator=(const CoroutineFrameControl&) = delete;

    [[nodiscard]] ExecutionLease tryAcquire(std::coroutine_handle<> handle) noexcept {
        std::lock_guard lock(mutex_);
        if (destroyed_ || !handle) {
            return {};
        }
        ++activeLeases_;
        return ExecutionLease(this);
    }

    bool isDone(std::coroutine_handle<> handle) const noexcept {
        std::lock_guard lock(mutex_);
        if (destroyed_ || !handle) {
            return true;
        }
        // handle.done() races with a frame running on another thread; use finished_ instead.
        return tracksFinalSuspend_ ? finished_ : handle.done();
    }

    // Called from final_suspend(), before the frame is suspended.
    void markFinalizing() noexcept {
        std::lock_guard lock(mutex_);
        finalizing_ = true;
    }

    // The final awaiter's last access to the promise: afterwards the frame may be destroyed.
    template <typename F>
    void finish(F&& takeOwnership) noexcept {
        std::coroutine_handle<> deferredHandle;
        {
            std::lock_guard lock(mutex_);
            takeOwnership();
            finished_ = true;
            if (destroyed_ && activeCount() == 0) {
                deferredHandle = std::exchange(deferredHandle_, nullptr);
            }
        }
        detail::deferCoroutineDestroy(deferredHandle);
    }

    enum class Phase : uint8_t { Running, Finalizing, Finished };

    // Links unless Finished, and reports the phase: only a Running frame may be resumed.
    template <typename F>
    [[nodiscard]] Phase linkUnlessFinished(F&& link) noexcept {
        std::lock_guard lock(mutex_);
        if (finished_) {
            return Phase::Finished;
        }
        link();
        return finalizing_ ? Phase::Finalizing : Phase::Running;
    }

    // Releases single-execution ownership. Called when the frame is about to park on an
    // external wait or reaches its final suspend. Idempotent.
    void endExecution() noexcept {
        std::lock_guard lock(mutex_);
        running_ = false;
    }

    // Number of resumes refused because the frame was already executing. Non-zero means a
    // caller tried to drive the same frame from more than one execution context.
    [[nodiscard]] uint32_t executionConflicts() const noexcept {
        std::lock_guard lock(mutex_);
        return executionConflicts_;
    }

    bool resume(std::coroutine_handle<> handle) noexcept {
        std::coroutine_handle<> deferredHandle;
        bool outermost = false;
        {
            std::unique_lock lock(mutex_);
            if (destroyed_ || !handle || handle.done()) {
                return false;
            }
            if (running_) {
                ++executionConflicts_;
                return false;
            }
            running_ = true;
            ++activeResumes_;
            outermost = detail::coroutineResumeDepth == 0;
            ++detail::coroutineResumeDepth;
            handle.resume();
            --detail::coroutineResumeDepth;
            --activeResumes_;
            running_ = false;
            if (activeCount() == 0 && !finalizingLocked()) {
                deferredHandle = std::exchange(deferredHandle_, nullptr);
            }
        }

        detail::deferCoroutineDestroy(deferredHandle);
        if (outermost) {
            detail::drainDeferredCoroutineDestroys();
        }
        return true;
    }

    // Returns false when the frame is still protected by an active resume or lease.
    bool destroy(std::coroutine_handle<> handle) noexcept {
        std::lock_guard lock(mutex_);
        if (destroyed_ || !handle) {
            return false;
        }
        destroyed_ = true;
        if (activeCount() == 0 && !finalizingLocked()) {
            return true;
        }
        deferredHandle_ = handle;
        return false;
    }

 private:
    [[nodiscard]] uint32_t activeCount() const noexcept {
        return activeResumes_ + activeLeases_;
    }

    [[nodiscard]] bool finalizingLocked() const noexcept {
        return finalizing_ && !finished_;
    }

    void releaseLease() noexcept {
        std::coroutine_handle<> deferredHandle;
        {
            std::unique_lock lock(mutex_);
            if (activeLeases_ == 0) {
                return;
            }
            --activeLeases_;
            if (activeCount() == 0 && !finalizingLocked()) {
                deferredHandle = std::exchange(deferredHandle_, nullptr);
            }
        }
        detail::deferCoroutineDestroy(deferredHandle);
    }

    mutable std::recursive_mutex mutex_;
    const bool tracksFinalSuspend_{false};
    bool finalizing_{false};
    bool finished_{false};
    bool destroyed_{false};
    bool running_{false};
    uint32_t activeResumes_{0};
    uint32_t activeLeases_{0};
    uint32_t executionConflicts_{0};
    std::coroutine_handle<> deferredHandle_;

    friend class ExecutionLease;
};

inline void ExecutionLease::reset() noexcept {
    auto* control = std::exchange(control_, nullptr);
    if (control) {
        control->releaseLease();
    }
}

template <typename Promise>
concept ContinuationPromise = requires(Promise& promise) {
    { promise.frameControl() } -> std::same_as<std::shared_ptr<CoroutineFrameControl>>;
    { promise.continuationLeaseSlot() } -> std::same_as<ExecutionLease*>;
    { promise.incomingTransferLeaseSlot() } -> std::same_as<ExecutionLease*>;
};

struct ContinuationLink final {
    std::coroutine_handle<> handle;
    std::shared_ptr<CoroutineFrameControl> control;
    ExecutionLease lease;
    ExecutionLease* targetLeaseSlot = nullptr;
    ExecutionLease* sourceLeaseSlot = nullptr;

    ContinuationLink() noexcept = default;

    ContinuationLink(const ContinuationLink&) = delete;
    ContinuationLink& operator=(const ContinuationLink&) = delete;

    ContinuationLink(ContinuationLink&& other) noexcept
        : handle(std::exchange(other.handle, nullptr)),
          control(std::move(other.control)),
          lease(std::move(other.lease)),
          targetLeaseSlot(std::exchange(other.targetLeaseSlot, nullptr)),
          sourceLeaseSlot(std::exchange(other.sourceLeaseSlot, nullptr)) {}

    ContinuationLink& operator=(ContinuationLink&& other) noexcept {
        if (this != &other) {
            reset();
            handle = std::exchange(other.handle, nullptr);
            control = std::move(other.control);
            lease = std::move(other.lease);
            targetLeaseSlot = std::exchange(other.targetLeaseSlot, nullptr);
            sourceLeaseSlot = std::exchange(other.sourceLeaseSlot, nullptr);
        }
        return *this;
    }

    ~ContinuationLink() {
        reset();
    }

    static ContinuationLink create(std::coroutine_handle<> target,
                                   std::shared_ptr<CoroutineFrameControl> targetControl,
                                   ExecutionLease* targetSlot,
                                   ExecutionLease* sourceSlot = nullptr) noexcept {
        auto link = registerTarget(target, std::move(targetControl), targetSlot, sourceSlot);
        if (!link.isRegistered() || !link.acquireTarget()) {
            link.reset();
        }
        return link;
    }

    static ContinuationLink registerTarget(std::coroutine_handle<> target,
                                           std::shared_ptr<CoroutineFrameControl> targetControl,
                                           ExecutionLease* targetSlot,
                                           ExecutionLease* sourceSlot = nullptr) noexcept {
        ContinuationLink link;
        if (!target || !targetControl || !targetSlot || targetControl->isDone(target)) {
            return link;
        }
        link.handle = target;
        link.control = std::move(targetControl);
        link.targetLeaseSlot = targetSlot;
        link.sourceLeaseSlot = sourceSlot;
        return link;
    }

    [[nodiscard]] bool isRegistered() const noexcept {
        return handle && control && targetLeaseSlot && !control->isDone(handle);
    }

    [[nodiscard]] bool acquireTarget() noexcept {
        if (!isRegistered() || lease) {
            return static_cast<bool>(lease);
        }
        lease = control->tryAcquire(handle);
        return static_cast<bool>(lease);
    }

    [[nodiscard]] bool isValid() const noexcept {
        return isRegistered() && lease;
    }

    explicit operator bool() const noexcept {
        return isValid();
    }

    void reset() noexcept {
        targetLeaseSlot = nullptr;
        sourceLeaseSlot = nullptr;
        lease.reset();
        handle = nullptr;
        control.reset();
    }
};

class ResumeGate final {
 public:
    enum class State : uint8_t {
        Pending,
        Armed,
        Signaled,
        Resuming,
        Disarmed,
    };

    ResumeGate(std::coroutine_handle<> handle,
               std::shared_ptr<CoroutineFrameControl> frameControl) noexcept
        : handle_(handle), frameControl_(std::move(frameControl)) {}

    ResumeGate(const ResumeGate&) = delete;
    ResumeGate& operator=(const ResumeGate&) = delete;

    bool arm() noexcept {
        std::lock_guard lock(mutex_);
        if (state_ == State::Pending) {
            state_ = State::Armed;
            return true;
        }
        return false;
    }

    void signal() noexcept {
        std::coroutine_handle<> handle;
        std::shared_ptr<CoroutineFrameControl> frameControl;
        {
            std::lock_guard lock(mutex_);
            if (state_ == State::Pending) {
                state_ = State::Signaled;
                return;
            }
            if (state_ != State::Armed) {
                return;
            }
            state_ = State::Resuming;
            handle = handle_;
            frameControl = frameControl_;
        }

        dispatchResumeLocked(std::move(frameControl), handle);
    }

    void disarm() noexcept {
        std::lock_guard lock(mutex_);
        if (state_ == State::Pending || state_ == State::Armed) {
            state_ = State::Disarmed;
        }
    }

    void signalTerminal() noexcept {
        settle(false);
    }

    void signalCancel() noexcept {
        settle(true);
    }

    [[nodiscard]] bool consumeNotReady() noexcept {
        std::lock_guard lock(mutex_);
        return std::exchange(notReady_, false);
    }

    [[nodiscard]] bool consumeCancelled() noexcept {
        std::lock_guard lock(mutex_);
        return std::exchange(cancelled_, false);
    }

    void setExecutor(std::shared_ptr<IExecutor> executor) noexcept {
        executor_ = std::move(executor);
    }

 private:
    void settle(bool cancelled) noexcept {
        std::shared_ptr<CoroutineFrameControl> frameControl;
        std::coroutine_handle<> handle;
        {
            std::unique_lock lock(mutex_);
            if (!cancelled) {
                notReady_ = true;
            }
            if (state_ != State::Pending && state_ != State::Armed) {
                return;
            }
            cancelled_ = cancelled;
            if (state_ == State::Pending) {
                state_ = State::Signaled;
                return;
            }
            state_ = State::Resuming;
            frameControl = frameControl_;
            handle = handle_;
        }

        dispatchResumeLocked(std::move(frameControl), handle);
    }

    void dispatchResumeLocked(std::shared_ptr<CoroutineFrameControl> frameControl,
                              std::coroutine_handle<> handle) noexcept {
        if (!frameControl) {
            return;
        }
        auto& metrics = CoroutineScheduler::instance().resumeMetrics();
        const auto executor = executor_;
        if (executor == nullptr || executor->isInline()) {
            metrics.inlined.fetch_add(1, std::memory_order_relaxed);
            frameControl->resume(handle);
            return;
        }
        metrics.dispatched.fetch_add(1, std::memory_order_relaxed);
        metrics.pending.fetch_add(1, std::memory_order_relaxed);
        const auto enqueuedAt = std::chrono::steady_clock::now();
        const bool accepted =
            executor->tryExecute([frameControl, handle, executor, enqueuedAt]() noexcept {
                auto& running = CoroutineScheduler::instance().resumeMetrics();
                running.pending.fetch_sub(1, std::memory_order_relaxed);
                running.recordQueueLatency(enqueuedAt);
                frameControl->resume(handle);
            });
        if (accepted) {
            return;
        }
        metrics.pending.fetch_sub(1, std::memory_order_relaxed);
        metrics.refused.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(mutex_);
            cancelled_ = true;
            notReady_ = true;
        }
        auto& fallback = CoroutineScheduler::instance().fallbackExecutor();
        const bool resumedOnFallback = fallback.tryExecute([frameControl, handle]() noexcept {
            frameControl->resume(handle);
        });
        if (!resumedOnFallback) {
            LogE("coroutine resume refused by the fallback executor; aborting");
            std::terminate();
        }
    }

    mutable std::mutex mutex_;
    std::coroutine_handle<> handle_;
    std::shared_ptr<CoroutineFrameControl> frameControl_;
    std::shared_ptr<IExecutor> executor_;
    State state_{State::Pending};
    bool notReady_{false};
    bool cancelled_{false};
};

template <typename Promise>
    requires requires(Promise& promise) { promise.frameControl(); }
std::shared_ptr<ResumeGate> makeResumeGate(std::coroutine_handle<Promise> handle) noexcept {
    auto gate = std::make_shared<ResumeGate>(handle, handle.promise().frameControl());
    if constexpr (requires {
                      {
                          handle.promise().executor()
                      } -> std::convertible_to<std::shared_ptr<IExecutor>>;
                  }) {
        gate->setExecutor(handle.promise().executor());
    }
    return gate;
}

template <typename Promise>
void registerExternalGate(Promise& promise, std::shared_ptr<ResumeGate> gate) noexcept {
    if constexpr (requires { promise.setExternalGate(gate); }) {
        promise.setExternalGate(std::move(gate));
    }
}

template <typename Promise>
void releaseIncomingTransferLease(Promise& promise) noexcept {
    if constexpr (requires { promise.releaseIncomingTransferLease(); }) {
        promise.releaseIncomingTransferLease();
    }
}

template <typename Promise>
void releaseTransferLeases(Promise& promise) noexcept {
    // The frame is about to park (or finish), so it no longer owns execution.
    if constexpr (requires { promise.frameControl(); }) {
        promise.frameControl()->endExecution();
    }
    releaseIncomingTransferLease(promise);
    if constexpr (requires { promise.releaseContinuationLease(); }) {
        promise.releaseContinuationLease();
    }
    if constexpr (requires { promise.releaseTransferLease(); }) {
        promise.releaseTransferLease();
    }
}

template <typename Promise>
void markExternalAwait(Promise& promise) noexcept {
    if constexpr (requires { promise.markWaitingExternal(); }) {
        promise.markWaitingExternal();
    }
}

template <typename Promise>
void markCoroutineExecuting(Promise& promise) noexcept {
    if constexpr (requires { promise.markExecuting(); }) {
        promise.markExecuting();
    }
}

class PromiseCancellation {
 public:
    [[nodiscard]] const CancellationToken* cancelToken() const noexcept {
        return token_ ? &*token_ : nullptr;
    }

    void setCancelToken(CancellationToken token) noexcept {
        token_ = std::move(token);
    }

    void inheritCancelToken(const CancellationToken* parent) {
        if (!parent) {
            return;
        }
        if (token_) {
            token_ = CancellationToken::link(*token_, *parent);
        } else {
            token_ = *parent;
        }
    }

    // Bindings must precede the first resume; a later one would be silently lost.
    [[nodiscard]] bool hasStarted() const noexcept {
        return started_.load(std::memory_order_acquire);
    }

    void markStarted() noexcept {
        started_.store(true, std::memory_order_release);
    }

 protected:
    std::optional<CancellationToken> token_;
    std::atomic<bool> started_{false};
};

template <typename Promise>
struct FrameStartAwaiter {
    Promise* promise;

    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    void await_resume() const noexcept {
        promise->markStarted();
    }
};

namespace detail {

inline void requireNotStarted(const PromiseCancellation& promise, const char* message) {
    if (promise.hasStarted()) {
        throw std::logic_error(message);
    }
}

}  // namespace detail

template <typename Promise, typename OnCancel>
[[nodiscard]] CancellationTokenRegistration bindCancellation(Promise& promise, OnCancel onCancel) {
    if constexpr (requires { promise.cancelToken(); }) {
        if (const auto* token = promise.cancelToken()) {
            return token->registerCallbackOrRun(std::move(onCancel));
        }
    }
    return {};
}

template <typename Promise>
[[nodiscard]] CancellationTokenRegistration bindGateCancellation(
    Promise& promise,
    const std::shared_ptr<ResumeGate>& gate) {
    return bindCancellation(promise, [weak = std::weak_ptr<ResumeGate>(gate)] {
        if (auto locked = weak.lock()) {
            locked->signalCancel();
        }
    });
}

}  // namespace slark
