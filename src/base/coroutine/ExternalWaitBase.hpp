// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <coroutine>
#include <memory>
#include <optional>
#include <utility>

#include "CancellationToken.h"
#include "CoroutineControl.hpp"
#include "CoroutineScheduler.hpp"
#include "Error.h"

namespace slark {

// Cancels the scheduled callback when destroyed, so an abandoned wait cannot be signalled later.
class ScheduledCallback final {
public:
    ScheduledCallback() noexcept = default;

    explicit ScheduledCallback(TimerId id) noexcept : id_(id) {}

    ScheduledCallback(ScheduledCallback&& other) noexcept : id_(std::exchange(other.id_, {})) {}

    ScheduledCallback& operator=(ScheduledCallback&& other) noexcept {
        if (this != &other) {
            reset();
            id_ = std::exchange(other.id_, {});
        }
        return *this;
    }

    ScheduledCallback(const ScheduledCallback&) = delete;
    ScheduledCallback& operator=(const ScheduledCallback&) = delete;

    ~ScheduledCallback() {
        reset();
    }

private:
    void reset() noexcept {
        if (id_.isValid()) {
            CoroutineScheduler::instance().cancel(id_);
            id_ = {};
        }
    }

    TimerId id_;
};

// Signals the gate after the delay. The callback holds the gate weakly.
inline ScheduledCallback scheduleGateSignal(std::chrono::milliseconds delay,
                                            const std::shared_ptr<ResumeGate>& gate) {
    return ScheduledCallback(CoroutineScheduler::instance().scheduleCallback(
        delay, [weak = std::weak_ptr<ResumeGate>(gate)]() {
            if (auto locked = weak.lock()) {
                locked->signal();
            }
        }));
}

// Suspension protocol shared by leaf awaiters that wait on a single gate. A derived awaiter
// supplies:
//   bool ready() const noexcept             true skips the suspension entirely
//   std::optional<Source> start(gate)       registers the event source(s) that signal the gate;
//                                           nullopt when the event has already happened, so the
//                                           frame keeps running
//   Result finish()                         maps the outcome once the frame has resumed
//   static std::string_view cancelMessage() text of the CancelledException
// Source is a movable RAII handle: destroying it unregisters the event source. The base owns the
// gate, the transfer leases, the token registration and the arm/resume race, so every leaf wait
// cancels and tears down the same way.
template <typename Derived, typename Source, typename Result = void>
class ExternalWaitBase {
public:
    ExternalWaitBase() = default;
    ExternalWaitBase(const ExternalWaitBase&) = delete;
    ExternalWaitBase& operator=(const ExternalWaitBase&) = delete;

    ~ExternalWaitBase() {
        // Disarm before the sources unregister: nothing may resume a frame that is going away.
        if (gate_) {
            gate_->disarm();
        }
    }

    [[nodiscard]] bool await_ready() const noexcept {
        return derived().ready();
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        gate_ = enterExternalWait(handle);
        source_ = derived().start(gate_);
        if (!source_) {
            gate_->disarm();
            return false;
        }
        cancelReg_ = attach(handle.promise(), LeafEdge{},
                            [weak = std::weak_ptr<ResumeGate>(gate_)] {
                                if (auto locked = weak.lock()) {
                                    locked->signalCancel();
                                }
                            });
        return gate_->arm();
    }

    Result await_resume() {
        if (gate_ && gate_->consumeCancelled()) {
            throw CancelledException(Derived::cancelMessage());
        }
        return derived().finish();
    }

protected:
    [[nodiscard]] const std::shared_ptr<ResumeGate>& gate() const noexcept {
        return gate_;
    }

private:
    [[nodiscard]] Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    [[nodiscard]] const Derived& derived() const noexcept {
        return static_cast<const Derived&>(*this);
    }

    std::shared_ptr<ResumeGate> gate_;
    std::optional<Source> source_;
    CancellationTokenRegistration cancelReg_;
};

}  // namespace slark
