// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"

namespace slark {

struct IExecutor {
    virtual ~IExecutor() = default;
    virtual void execute(MoveOnlyFunction<void()> task) noexcept = 0;

    // Same as execute() but reports whether the task was accepted. Executors that can
    // refuse work after shutdown must override it, so a refused dispatch is detectable
    // instead of silently dropping a wake-up.
    virtual bool tryExecute(MoveOnlyFunction<void()> task) noexcept {
        execute(std::move(task));
        return true;
    }

    virtual bool isInline() const noexcept {
        return false;
    }
};

struct InlineExecutor final : IExecutor {
    void execute(MoveOnlyFunction<void()> task) noexcept override {
        if (task) {
            task();
        }
    }

    bool isInline() const noexcept override {
        return true;
    }
};

// Serializes every dispatched task onto one worker thread: FIFO, never inline, at most one
// task running at a time. Tasks posted from within a running task are queued, not reentered.
// This is the "execution domain" a coroutine can be bound to, so that resuming a frame never
// happens on an arbitrary event-source thread.
class SerialExecutor final : public IExecutor, public NonCopyable {
 public:
    SerialExecutor();
    ~SerialExecutor() override;

    void execute(MoveOnlyFunction<void()> task) noexcept override;
    bool tryExecute(MoveOnlyFunction<void()> task) noexcept override;

    bool isInline() const noexcept override {
        return false;
    }

    // Stop accepting new work; the worker drains queued tasks before it exits.
    void requestStop() noexcept;

 private:
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<MoveOnlyFunction<void()>> queue;
        bool stopRequested{false};
    };

    static void runLoop(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;
    std::thread worker_;
};

inline SerialExecutor::SerialExecutor() : state_(std::make_shared<State>()) {
    worker_ = std::thread([state = state_] {
        runLoop(state);
    });
}

inline SerialExecutor::~SerialExecutor() {
    requestStop();
    if (worker_.joinable()) {
        if (worker_.get_id() == std::this_thread::get_id()) {
            worker_.detach();
        } else {
            worker_.join();
        }
    }
}

inline void SerialExecutor::execute(MoveOnlyFunction<void()> task) noexcept {
    (void)tryExecute(std::move(task));
}

inline bool SerialExecutor::tryExecute(MoveOnlyFunction<void()> task) noexcept {
    if (!task) {
        return false;
    }
    const auto state = state_;
    {
        std::lock_guard lock(state->mutex);
        if (state->stopRequested) {
            return false;
        }
        state->queue.push_back(std::move(task));
    }
    state->cv.notify_one();
    return true;
}

inline void SerialExecutor::requestStop() noexcept {
    const auto state = state_;
    {
        std::lock_guard lock(state->mutex);
        state->stopRequested = true;
    }
    state->cv.notify_one();
}

inline void SerialExecutor::runLoop(std::shared_ptr<State> state) noexcept {
    while (true) {
        MoveOnlyFunction<void()> task;
        {
            std::unique_lock lock(state->mutex);
            state->cv.wait(lock, [&state] {
                return !state->queue.empty() || state->stopRequested;
            });
            if (state->queue.empty()) {
                if (state->stopRequested) {
                    return;
                }
                continue;
            }
            task = std::move(state->queue.front());
            state->queue.pop_front();
        }
        if (task) {
            task();
        }
    }
}

}  // namespace slark
