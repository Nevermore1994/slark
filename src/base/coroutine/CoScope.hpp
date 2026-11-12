// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CancellationToken.h"
#include "Error.h"
#include "Executor.h"
#include "Log.hpp"
#include "Task.hpp"

namespace slark::coro {

namespace detail {

inline bool isCancellation(const std::exception_ptr& error) noexcept {
    if (!error) {
        return false;
    }
    try {
        std::rethrow_exception(error);
    } catch (const slark::CancelledException&) {
        return true;
    } catch (...) {
        return false;
    }
}

inline std::string describeException(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown exception";
    }
}

struct ScopeCore {
    std::mutex mutex;
    slark::CancellationToken cancelToken;
    IExecutor* defaultExecutor = nullptr;
    std::unordered_map<std::uint64_t, Task<void>> children;
    std::vector<std::shared_ptr<ResumeGate>> joinWaiters;
    std::exception_ptr failure;
    bool failureObserved = false;

    void recordFailure(std::exception_ptr error) noexcept {
        if (isCancellation(error)) {
            return;
        }
        bool first = false;
        {
            std::lock_guard lock(mutex);
            if (!failure) {
                failure = error;
                first = true;
            }
        }
        if (first) {
            cancelToken.cancel();
        }
    }
};

inline void retireChild(const std::shared_ptr<ScopeCore>& core, std::uint64_t id) noexcept {
    Task<void> finished;
    std::vector<std::shared_ptr<ResumeGate>> toSignal;
    {
        std::lock_guard lock(core->mutex);
        if (auto node = core->children.extract(id); !node.empty()) {
            finished = std::move(node.mapped());
        }
        if (core->children.empty()) {
            toSignal.swap(core->joinWaiters);
        }
    }
    for (const auto& gate : toSignal) {
        gate->signal();
    }
}

template <typename T>
Task<void> scopeWrapper(std::shared_ptr<ScopeCore> core, std::uint64_t id, Task<T> task) {
    try {
        if constexpr (std::is_same_v<T, void>) {
            co_await std::move(task);
        } else {
            [[maybe_unused]] auto _ = co_await std::move(task);
        }
    } catch (...) {
        core->recordFailure(std::current_exception());
    }
    retireChild(core, id);
    co_return;
}

}  // namespace detail

class JoinAwaiter {
 public:
    explicit JoinAwaiter(std::shared_ptr<detail::ScopeCore> core) noexcept
        : core_(std::move(core)) {}

    JoinAwaiter(const JoinAwaiter&) = delete;
    JoinAwaiter& operator=(const JoinAwaiter&) = delete;
    JoinAwaiter(JoinAwaiter&&) = delete;
    JoinAwaiter& operator=(JoinAwaiter&&) = delete;

    ~JoinAwaiter() {
        if (gate_) {
            gate_->disarm();
        }
    }

    bool await_ready() const noexcept {
        std::lock_guard lock(core_->mutex);
        return core_->children.empty();
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        releaseTransferLeases(handle.promise());
        markExternalAwait(handle.promise());
        gate_ = makeResumeGate(handle);
        {
            std::lock_guard lock(core_->mutex);
            if (core_->children.empty()) {
                gate_->disarm();
                return false;
            }
            core_->joinWaiters.push_back(gate_);
        }
        registerExternalGate(handle.promise(), gate_);
        cancelReg_ = bindGateCancellation(handle.promise(), gate_);
        return gate_->arm();
    }

    void await_resume() {
        if (gate_ && gate_->consumeCancelled()) {
            throw CancelledException("Scope join cancelled");
        }
        std::exception_ptr failure;
        {
            std::lock_guard lock(core_->mutex);
            failure = core_->failure;
            if (failure) {
                core_->failureObserved = true;
            }
        }
        if (failure) {
            std::rethrow_exception(failure);
        }
    }

 private:
    std::shared_ptr<detail::ScopeCore> core_;
    std::shared_ptr<ResumeGate> gate_;
    CancellationTokenRegistration cancelReg_;
};

class AsyncScope {
 public:
    AsyncScope() : core_(std::make_shared<detail::ScopeCore>()) {}

    AsyncScope(const AsyncScope&) = delete;
    AsyncScope& operator=(const AsyncScope&) = delete;
    AsyncScope(AsyncScope&&) = delete;
    AsyncScope& operator=(AsyncScope&&) = delete;

    ~AsyncScope() {
        core_->cancelToken.cancel();
        std::size_t running = 0;
        std::exception_ptr unobserved;
        {
            std::lock_guard lock(core_->mutex);
            running = core_->children.size();
            if (!core_->failureObserved) {
                unobserved = core_->failure;
            }
        }
        if (running > 0) {
            LogW("AsyncScope destroyed with {} running child task(s); cancelled and detached",
                 running);
        }
        if (unobserved) {
            LogE("AsyncScope failed and join() was never called: {}",
                 detail::describeException(unobserved));
        }
    }

    template <typename T>
    bool spawn(Task<T> task) {
        return spawn(std::move(task), core_->cancelToken);
    }

    template <typename T>
    bool spawn(Task<T> task, slark::CancellationToken token) {
        const auto id = nextId_.fetch_add(1, std::memory_order_relaxed);
        auto wrapperTask = detail::scopeWrapper<T>(core_, id, std::move(task));
        wrapperTask.setCancellationToken(std::move(token));
        auto handle = wrapperTask.getHandle();
        auto frameControl = handle.promise().frameControl();
        {
            std::lock_guard lock(core_->mutex);
            core_->children.emplace(id, std::move(wrapperTask));
        }

        if (core_->defaultExecutor && !core_->defaultExecutor->isInline()) {
            const bool accepted =
                core_->defaultExecutor->tryExecute([frameControl, handle]() noexcept {
                    frameControl->resume(handle);
                });
            if (!accepted) {
                detail::retireChild(core_, id);
                return false;
            }
        } else {
            frameControl->resume(handle);
        }
        return true;
    }

    [[nodiscard]] JoinAwaiter join() noexcept {
        return JoinAwaiter(core_);
    }

    void cancelAll() noexcept {
        core_->cancelToken.cancel();
    }

    void setDefaultExecutor(IExecutor& exec) noexcept {
        core_->defaultExecutor = &exec;
    }

    [[nodiscard]] slark::CancellationToken token() const noexcept {
        return core_->cancelToken;
    }

 private:
    friend struct AsyncScopeTestAccess;

    std::shared_ptr<detail::ScopeCore> core_;
    std::atomic<std::uint64_t> nextId_{0};
};

}  // namespace slark::coro
