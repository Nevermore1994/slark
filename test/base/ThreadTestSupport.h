//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

namespace slark::test {

// Runs fn on a detached thread with a bounded wait: a regression that wedges a
// worker fails the test instead of hanging the suite.
template <class Fn>
auto withWatchdog(Fn&& fn, std::chrono::seconds timeout = std::chrono::seconds(5))
    -> decltype(fn()) {
    std::packaged_task<decltype(fn())()> task(std::forward<Fn>(fn));
    auto future = task.get_future();
    std::thread runner(std::move(task));
    auto status = future.wait_for(timeout);
    runner.detach();
    if (status != std::future_status::ready) {
        throw std::runtime_error("withWatchdog timeout");
    }
    return future.get();
}

class BlockingTask {
 public:
    BlockingTask() : state_(std::make_shared<State>()) {}

    void operator()() const {
        state_->entered.set_value();
        state_->releaseFuture.wait();
    }

    std::future<void> takeEnteredFuture() {
        return state_->entered.get_future();
    }

    void release() {
        state_->release.set_value();
    }

 private:
    struct State {
        State() : releaseFuture(release.get_future().share()) {}

        std::promise<void> entered;
        std::promise<void> release;
        std::shared_future<void> releaseFuture;
    };

    std::shared_ptr<State> state_;
};

// Captured into posted tasks; use_count() must drop back to 1 once the task
// has executed or been dropped, proving no task-object leaks.
class TrackedToken : public std::enable_shared_from_this<TrackedToken> {
 public:
    static std::shared_ptr<TrackedToken> create() {
        return std::shared_ptr<TrackedToken>(new TrackedToken());
    }

    std::shared_ptr<TrackedToken> get() { return shared_from_this(); }

 private:
    TrackedToken() = default;
};

}  // namespace slark::test
