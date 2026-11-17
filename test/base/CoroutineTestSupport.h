// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "AsyncIOOperation.hpp"
#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "CoScope.hpp"
#include "Error.h"
#include "Executor.h"
#include "Task.hpp"

namespace slark {

struct CancellationTokenTestAccess {
    static std::size_t callbackCount(const CancellationToken& token) {
        std::lock_guard lock(token.state_->mutex);
        return token.state_->callbackMap.size();
    }

    static std::size_t linkDepth(const CancellationToken& token) {
        return depthOf(*token.state_);
    }

private:
    static std::size_t depthOf(const CancellationToken::State& state) {
        std::size_t deepest = 0;
        for (const auto& source : state.linkSources) {
            deepest = std::max(deepest, 1 + depthOf(*source));
        }
        return deepest;
    }
};

}  // namespace slark

namespace slark::test {

inline std::thread::id workerThreadOf(SerialExecutor& executor) {
    std::promise<std::thread::id> probe;
    auto worker = probe.get_future();
    executor.execute([p = std::move(probe)]() mutable {
        p.set_value(std::this_thread::get_id());
    });
    return worker.get();
}

struct ThreadsAroundTimer {
    std::thread::id before;
    std::thread::id after;
};

inline Task<void> recordThreadsAroundTimer(std::shared_ptr<ThreadsAroundTimer> out) {
    out->before = std::this_thread::get_id();
    co_await TimerAwaiter(std::chrono::milliseconds(2));
    out->after = std::this_thread::get_id();
}

template <typename T>
bool waitUntilDone(const Task<T>& task, std::chrono::seconds timeout = std::chrono::seconds(3)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return task.isDone();
}

inline Task<void> joinScopeTask(slark::coro::AsyncScope& scope) {
    co_await scope.join();
}

inline bool joinScope(slark::coro::AsyncScope& scope,
                      std::chrono::seconds timeout = std::chrono::seconds(3)) {
    auto join = joinScopeTask(scope);
    join.resume();
    return waitUntilDone(join, timeout);
}

template <typename T>
bool waitUntilExpired(const std::weak_ptr<T>& watch,
                      std::chrono::seconds timeout = std::chrono::seconds(3)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!watch.expired() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return watch.expired();
}

inline bool waitUntilSet(const std::atomic<bool>& flag,
                         std::chrono::seconds timeout = std::chrono::seconds(3)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return flag.load();
}

class ManualIO final : public AsyncIOOperationBase<ManualIO, std::string> {
public:
    void finish(std::string value) {
        complete(std::move(value));
    }

    void abort(Error error) {
        fail(error);
    }

    void storeResult(std::string value) noexcept {
        result_ = std::move(value);
    }

    [[nodiscard]] std::string getResult() const {
        return result_;
    }

protected:
    void startOperation() noexcept override {}

private:
    std::string result_;
};

}  // namespace slark::test
