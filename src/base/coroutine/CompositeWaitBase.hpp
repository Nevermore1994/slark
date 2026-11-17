// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <coroutine>
#include <memory>

#include "CancellationToken.h"
#include "CoroutineControl.hpp"
#include "Executor.h"

namespace slark {

template <typename Derived>
class CompositeWaitBase {
public:
    CompositeWaitBase() = default;
    CompositeWaitBase(const CompositeWaitBase&) = delete;
    CompositeWaitBase& operator=(const CompositeWaitBase&) = delete;

    ~CompositeWaitBase() {
        disarmGate();
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto& promise = handle.promise();
        releaseTransferLeases(promise);
        markExternalAwait(promise);
        gate_ = makeResumeGate(handle);
        registerExternalGate(promise, gate_);
        // The awaiting coroutine's token flows down to the children; it never wakes this wait.
        derived().start(gate_, cancellationTokenOf(promise), executorOf(promise));
        return gate_->arm();
    }

protected:
    void disarmGate() noexcept {
        if (gate_) {
            gate_->disarm();
        }
    }

private:
    [[nodiscard]] Derived& derived() noexcept {
        return static_cast<Derived&>(*this);
    }

    std::shared_ptr<ResumeGate> gate_;
};

}  // namespace slark
