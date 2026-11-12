// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <chrono>
#include <string>
#include <thread>

#include "AsyncIOOperation.hpp"
#include "CoScope.hpp"
#include "Error.h"
#include "Task.hpp"

namespace slark::test {

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
