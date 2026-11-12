// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <chrono>
#include <coroutine>
#include <memory>

#include "CoroutineControl.hpp"
#include "CoroutineScheduler.hpp"
#include "Error.h"

namespace slark {

struct YieldAwaiter {
    ~YieldAwaiter() {
        if (gate_) {
            gate_->disarm();
        }
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        releaseTransferLeases(handle.promise());
        markExternalAwait(handle.promise());
        gate_ = makeResumeGate(handle);
        auto weakGate = std::weak_ptr<ResumeGate>(gate_);
        CoroutineScheduler::instance().scheduleCallback(std::chrono::milliseconds(0), [weakGate]() {
            if (auto gate = weakGate.lock()) {
                gate->signal();
            }
        });
        cancelReg_ = bindGateCancellation(handle.promise(), gate_);
        return gate_->arm();
    }

    void await_resume() {
        if (gate_ && gate_->consumeCancelled()) {
            throw CancelledException("Yield cancelled");
        }
    }

private:
    std::shared_ptr<ResumeGate> gate_;
    CancellationTokenRegistration cancelReg_;
};

inline YieldAwaiter yield() noexcept {
    return {};
}

}  // namespace slark
