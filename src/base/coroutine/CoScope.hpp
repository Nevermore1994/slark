// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include "CancellationToken.h"
#include "Executor.h"
#include "Task.hpp"

namespace slark::coro {

namespace detail {
struct ScopeState {
    std::atomic<int> activeCount{0};
    slark::CancellationToken cancelToken;
    std::vector<Task<void>> childTasks;
    std::mutex mutex;
    std::condition_variable cv;
    IExecutor* defaultExecutor = nullptr;
};

template <typename T>
Task<void> scopeWrapper(std::shared_ptr<ScopeState> state, Task<T> task);
}  // namespace detail

class AsyncScope {
 public:
    AsyncScope() : state_(std::make_shared<detail::ScopeState>()) {}

    ~AsyncScope() {
        state_->cancelToken.cancel();
        std::unique_lock lock(state_->mutex);
        if (!state_->cv.wait_for(lock, std::chrono::seconds(5), [s = state_]() {
                return s->activeCount.load(std::memory_order_acquire) == 0;
            })) {
            state_->childTasks.clear();
        }
    }

    template <typename T>
    void spawn(Task<T> task) {
        state_->activeCount.fetch_add(1, std::memory_order_relaxed);

        auto wrapperTask = detail::scopeWrapper<T>(state_, std::move(task));
        auto handle = wrapperTask.getHandle();
        state_->childTasks.push_back(std::move(wrapperTask));

        if (state_->defaultExecutor && !state_->defaultExecutor->isInline()) {
            state_->defaultExecutor->execute([h = handle]() noexcept {
                h.resume();
            });
        } else {
            handle.resume();
        }
    }

    void waitAll() noexcept {
        std::unique_lock lock(state_->mutex);
        state_->cv.wait_for(lock, std::chrono::seconds(5), [s = state_]() {
            return s->activeCount.load(std::memory_order_acquire) == 0;
        });
        if (state_->activeCount.load(std::memory_order_acquire) != 0) {
            state_->cancelToken.cancel();
        }
    }

    void cancelAll() noexcept {
        state_->cancelToken.cancel();
    }

    void setDefaultExecutor(IExecutor& exec) noexcept {
        state_->defaultExecutor = &exec;
    }

    [[nodiscard]] slark::CancellationToken token() const noexcept {
        return state_->cancelToken;
    }

 private:
    std::shared_ptr<detail::ScopeState> state_;
};

namespace detail {

template <typename T>
Task<void> scopeWrapper(std::shared_ptr<ScopeState> state, Task<T> task) {
    try {
        if constexpr (std::is_same_v<T, void>) {
            co_await std::move(task);
        } else {
            [[maybe_unused]] auto _ = co_await std::move(task);
        }
    } catch (...) {
    }
    state->activeCount.fetch_sub(1, std::memory_order_release);
    state->cv.notify_one();
    co_return;
}

}  // namespace detail

}  // namespace slark::coro
