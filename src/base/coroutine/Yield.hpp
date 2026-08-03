// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <chrono>
#include <coroutine>
#include <memory>

#include "CoroutineControl.hpp"
#include "CoroutineScheduler.hpp"

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
        gate_ = makeResumeGate(handle);
        auto weakGate = std::weak_ptr<ResumeGate>(gate_);
        CoroutineScheduler::instance().scheduleCallback(std::chrono::milliseconds(0), [weakGate]() {
            if (auto gate = weakGate.lock()) {
                gate->signal();
            }
        });
        return gate_->arm();
    }

    void await_resume() noexcept {}

 private:
    std::shared_ptr<ResumeGate> gate_;
};

inline YieldAwaiter yield() noexcept {
    return {};
}

}  // namespace slark
