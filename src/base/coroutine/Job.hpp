// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "Error.h"
#include "Executor.h"
#include "Task.hpp"

namespace slark::coro {

struct LaunchOptions {
    CancellationToken token;
    std::shared_ptr<IExecutor> executor;
};

template <typename T = void>
class Job;

template <typename T>
Job<T> makeJob(Task<T> task, LaunchOptions options = {});

namespace detail {

template <typename T>
struct JobState {
    using Value = std::conditional_t<std::is_void_v<T>, std::monostate, std::optional<T>>;

    std::mutex mutex;
    std::condition_variable settled;
    bool finished = false;
    std::exception_ptr error;
    Value value{};
    CompletionAwaiter<void> joined;
    Task<void> detachedBody;

    // Settles at most once. Waiters are released after the state is visible to waitFor.
    bool settle(std::exception_ptr failure) noexcept {
        {
            std::lock_guard lock(mutex);
            if (finished) {
                return false;
            }
            finished = true;
            error = std::move(failure);
        }
        settled.notify_all();
        joined.complete();
        return true;
    }

    // A body that outlives its destroyed Job is owned here until it ends.
    bool adopt(Task<void>& body) noexcept {
        std::lock_guard lock(mutex);
        if (finished) {
            return false;
        }
        detachedBody = std::move(body);
        return true;
    }

    void release() noexcept {
        Task<void> body;
        {
            std::lock_guard lock(mutex);
            body = std::move(detachedBody);
        }
    }
};

template <typename T>
Task<void> runJobBody(std::shared_ptr<JobState<T>> state, Task<T> task) {
    co_await RequireAttachedEdge{};
    std::exception_ptr failure;
    try {
        if constexpr (std::is_void_v<T>) {
            co_await std::move(task);
        } else {
            auto value = co_await std::move(task);
            std::lock_guard lock(state->mutex);
            state->value.emplace(std::move(value));
        }
    } catch (...) {
        failure = std::current_exception();
    }
    state->settle(std::move(failure));
    state->release();
}

}  // namespace detail

// Owns the root Task of an operation. Destroying a Job cancels it and releases its waiters
// without waiting for the body: the body stops at its next suspension point. Owner-thread
// operations are start, cancel, join and destruction; isDone and waitFor may be called from
// any thread, but waitFor must not run on the Job's own executor (it would wait for itself
// until the timeout).
template <typename T>
class [[nodiscard]] Job {
public:
    Job() noexcept = default;

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    Job(Job&& other) noexcept
        : state_(std::move(other.state_)),
          body_(std::move(other.body_)),
          own_(std::move(other.own_)),
          started_(std::exchange(other.started_, false)) {}

    Job& operator=(Job&& other) noexcept {
        if (this != &other) {
            detach();
            state_ = std::move(other.state_);
            body_ = std::move(other.body_);
            own_ = std::move(other.own_);
            started_ = std::exchange(other.started_, false);
        }
        return *this;
    }

    ~Job() {
        detach();
    }

    [[nodiscard]] bool isValid() const noexcept {
        return state_ != nullptr;
    }

    // An empty Job has nothing left to wait for, so it reports done (as Task does).
    [[nodiscard]] bool isDone() const noexcept {
        if (!state_) {
            return true;
        }
        std::lock_guard lock(state_->mutex);
        return state_->finished;
    }

    // Dispatches the first resume on the bound executor. A refused dispatch settles the job
    // with CancelledException without running the body. Idempotent.
    void start() {
        if (started_ || !body_.isValid()) {
            return;
        }
        started_ = true;
        const auto handle = body_.getHandle();
        const auto frame = handle.promise().frameControl();
        const auto& executor = handle.promise().executor();
        if (executor && !executor->isInline()) {
            const bool accepted = executor->tryExecute([frame, handle]() noexcept {
                frame->resume(handle);
            });
            if (!accepted) {
                state_->settle(std::make_exception_ptr(CancelledException("executor refused the job")));
            }
            return;
        }
        body_.resume();
    }

    void cancel() noexcept {
        if (state_) {
            own_.cancel();
        }
    }

    // Blocks until the job settles or the timeout passes. Returns true when settled.
    [[nodiscard]] bool waitFor(std::chrono::milliseconds timeout) const {
        if (!state_) {
            return true;
        }
        auto* state = state_.get();
        std::unique_lock lock(state->mutex);
        return state->settled.wait_for(lock, timeout, [state] { return state->finished; });
    }

    // Suspends until the job settles. Never throws the job's own failure; throws
    // CancelledException only when the joining coroutine itself is cancelled.
    [[nodiscard]] typename CompletionAwaiter<void>::Waiter join() {
        if (!state_) {
            throw std::logic_error("Job::join on an empty Job");
        }
        return state_->joined.operator co_await();
    }

    // Returns the value, or rethrows the failure. Only valid once the job has settled.
    T result() const requires(std::is_void_v<T> || std::copy_constructible<T>) {
        if (!state_) {
            throw std::runtime_error("Job is not valid");
        }
        std::lock_guard lock(state_->mutex);
        if (!state_->finished) {
            throw std::runtime_error("Job is not complete");
        }
        if (state_->error) {
            std::rethrow_exception(state_->error);
        }
        if constexpr (!std::is_void_v<T>) {
            return *state_->value;
        }
    }

private:
    friend Job<T> makeJob<T>(Task<T>, LaunchOptions);

    Job(std::shared_ptr<detail::JobState<T>> state, Task<void> body, CancellationToken own) noexcept
        : state_(std::move(state)), body_(std::move(body)), own_(std::move(own)) {}

    // The body's token is a child of the caller's, so either the caller or the Job cancels it.
    static Job create(Task<T> task, LaunchOptions options) {
        if (!task.isValid() || task.getHandle().promise().hasStarted()) {
            throw std::logic_error("coro::makeJob requires a valid Task that has not started");
        }
        auto state = std::make_shared<detail::JobState<T>>();
        Task<void> body = detail::runJobBody<T>(state, std::move(task));
        auto own = attach(body.getHandle().promise(), SpawnEdge{}, &options.token);
        if (options.executor) {
            body.setExecutor(std::move(options.executor));
        }
        return Job(std::move(state), std::move(body), std::move(own));
    }

    // Cancels, and settles the job as cancelled if it has not settled, so that joiners are
    // released even when the body never reaches its completion point.
    void detach() noexcept {
        if (!state_) {
            return;
        }
        own_.cancel();
        if (started_) {
            state_->adopt(body_);
        }
        state_->settle(std::make_exception_ptr(CancelledException("Job detached before completion")));
    }

    std::shared_ptr<detail::JobState<T>> state_;
    Task<void> body_;
    CancellationToken own_;
    bool started_ = false;
};

// Creates a Job without starting it.
template <typename T>
Job<T> makeJob(Task<T> task, LaunchOptions options) {
    return Job<T>::create(std::move(task), std::move(options));
}

// Creates and starts a Job.
template <typename T>
Job<T> launch(Task<T> task, LaunchOptions options = {}) {
    auto job = makeJob(std::move(task), std::move(options));
    job.start();
    return job;
}

}  // namespace slark::coro
