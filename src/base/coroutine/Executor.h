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

    // Stop accepting new work. The destructor joins the worker, which drains what is
    // already queued, so there is no separate drain step.
    void requestStop() noexcept;

 private:
    void runLoop() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<MoveOnlyFunction<void()>> queue_;
    bool stopRequested_{false};
    std::thread worker_;
};

inline SerialExecutor::SerialExecutor() {
    worker_ = std::thread([this] {
        runLoop();
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
    {
        std::lock_guard lock(mutex_);
        if (stopRequested_) {
            return false;
        }
        queue_.push_back(std::move(task));
    }
    cv_.notify_one();
    return true;
}

inline void SerialExecutor::requestStop() noexcept {
    {
        std::lock_guard lock(mutex_);
        stopRequested_ = true;
    }
    cv_.notify_one();
}

inline void SerialExecutor::runLoop() noexcept {
    while (true) {
        MoveOnlyFunction<void()> task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || stopRequested_;
            });
            if (queue_.empty()) {
                if (stopRequested_) {
                    return;
                }
                continue;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        if (task) {
            task();
        }
    }
}

}  // namespace slark
