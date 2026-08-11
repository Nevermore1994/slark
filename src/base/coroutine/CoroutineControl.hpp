// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <concepts>
#include <cstdint>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

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
        return destroyed_ || !handle || handle.done();
    }

    bool resume(std::coroutine_handle<> handle) noexcept {
        std::coroutine_handle<> deferredHandle;
        bool outermost = false;
        {
            std::unique_lock lock(mutex_);
            if (destroyed_ || !handle || handle.done()) {
                return false;
            }
            ++activeResumes_;
            outermost = detail::coroutineResumeDepth == 0;
            ++detail::coroutineResumeDepth;
            handle.resume();
            --detail::coroutineResumeDepth;
            --activeResumes_;
            if (activeCount() == 0) {
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
        if (activeCount() == 0) {
            return true;
        }
        deferredHandle_ = handle;
        return false;
    }

 private:
    [[nodiscard]] uint32_t activeCount() const noexcept {
        return activeResumes_ + activeLeases_;
    }

    void releaseLease() noexcept {
        std::coroutine_handle<> deferredHandle;
        {
            std::unique_lock lock(mutex_);
            if (activeLeases_ == 0) {
                return;
            }
            --activeLeases_;
            if (activeCount() == 0) {
                deferredHandle = std::exchange(deferredHandle_, nullptr);
            }
        }
        detail::deferCoroutineDestroy(deferredHandle);
    }

    mutable std::recursive_mutex mutex_;
    bool destroyed_{false};
    uint32_t activeResumes_{0};
    uint32_t activeLeases_{0};
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

    static ContinuationLink create(
        std::coroutine_handle<> target,
        std::shared_ptr<CoroutineFrameControl> targetControl,
        ExecutionLease* targetSlot,
        ExecutionLease* sourceSlot = nullptr) noexcept {
        auto link = registerTarget(target, std::move(targetControl), targetSlot, sourceSlot);
        if (!link.isRegistered() || !link.acquireTarget()) {
            link.reset();
        }
        return link;
    }

    static ContinuationLink registerTarget(
        std::coroutine_handle<> target,
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

        if (frameControl) {
            frameControl->resume(handle);
        }
    }

    void disarm() noexcept {
        std::lock_guard lock(mutex_);
        if (state_ == State::Pending || state_ == State::Armed) {
            state_ = State::Disarmed;
        }
    }

 private:
    mutable std::mutex mutex_;
    std::coroutine_handle<> handle_;
    std::shared_ptr<CoroutineFrameControl> frameControl_;
    State state_{State::Pending};
};

template <typename Promise>
    requires requires(Promise& promise) { promise.frameControl(); }
std::shared_ptr<ResumeGate> makeResumeGate(std::coroutine_handle<Promise> handle) noexcept {
    return std::make_shared<ResumeGate>(handle, handle.promise().frameControl());
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

}  // namespace slark
