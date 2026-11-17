// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CancellationToken.h"
#include "Error.h"
#include "Executor.h"
#include "ExternalWaitBase.hpp"
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

// Dispatches on an executor it does not keep alive. An accepted task holds the executor until it
// has run, so queued work keeps its worker alive; a dispatch after the executor is gone is refused.
class WeakExecutor final : public IExecutor {
public:
    explicit WeakExecutor(std::weak_ptr<IExecutor> target) : target_(std::move(target)) {
        if (auto live = target_.lock()) {
            inline_ = live->isInline();
        }
    }

    void execute(MoveOnlyFunction<void()> task) noexcept override {
        (void)tryExecute(std::move(task));
    }

    bool tryExecute(MoveOnlyFunction<void()> task) noexcept override {
        auto live = target_.lock();
        if (!live) {
            return false;
        }
        return live->tryExecute([live, task = std::move(task)]() mutable { task(); });
    }

    // Takes no strong reference: the dispatch path calls this under the resume gate's lock.
    bool isInline() const noexcept override {
        return inline_ && !target_.expired();
    }

private:
    std::weak_ptr<IExecutor> target_;
    bool inline_ = false;
};

struct ScopeCore {
    std::mutex mutex;
    slark::CancellationToken cancelToken;
    std::shared_ptr<IExecutor> defaultExecutor;
    bool spawned = false;
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

namespace detail {

// Join waiters stay in the scope's join list until it settles, so there is nothing to unlink.
struct JoinSlot {};

}  // namespace detail

class JoinAwaiter final : public ExternalWaitBase<JoinAwaiter, detail::JoinSlot> {
public:
    explicit JoinAwaiter(std::shared_ptr<detail::ScopeCore> core) noexcept
        : core_(std::move(core)) {}

    [[nodiscard]] bool ready() const noexcept {
        std::lock_guard lock(core_->mutex);
        return core_->children.empty();
    }

    [[nodiscard]] std::optional<detail::JoinSlot> start(const std::shared_ptr<ResumeGate>& gate) {
        std::lock_guard lock(core_->mutex);
        if (core_->children.empty()) {
            return std::nullopt;
        }
        core_->joinWaiters.push_back(gate);
        return detail::JoinSlot{};
    }

    void finish() {
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

    static const char* cancelMessage() noexcept {
        return "Scope join cancelled";
    }

private:
    std::shared_ptr<detail::ScopeCore> core_;
};

class AsyncScope {
 public:
    AsyncScope() : core_(std::make_shared<detail::ScopeCore>()) {}

    // Owning: children hold the domain until they finish, even after the scope is destroyed.
    explicit AsyncScope(std::shared_ptr<IExecutor> executor)
        : core_(std::make_shared<detail::ScopeCore>()) {
        core_->defaultExecutor = std::move(executor);
    }

    // Weak: the scope does not keep the executor alive; once it is gone, dispatch is refused.
    template <std::derived_from<IExecutor> E>
    explicit AsyncScope(std::weak_ptr<E> executor)
        : core_(std::make_shared<detail::ScopeCore>()) {
        core_->defaultExecutor = std::make_shared<detail::WeakExecutor>(std::move(executor));
    }

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
        return startChild(std::move(task), core_->cancelToken);
    }

    template <typename T>
    bool spawn(Task<T> task, slark::CancellationToken token) {
        return startChild(std::move(task), slark::CancellationToken::link(core_->cancelToken, token));
    }

    [[nodiscard]] JoinAwaiter join() noexcept {
        return JoinAwaiter(core_);
    }

    void cancelAll() noexcept {
        core_->cancelToken.cancel();
    }

    // Owning: children hold the domain until they finish, even after the scope is destroyed.
    void setDefaultExecutor(std::shared_ptr<IExecutor> exec) {
        setDomain(std::move(exec));
    }

    // Weak: the scope does not keep the executor alive; once it is gone, dispatch is refused.
    template <std::derived_from<IExecutor> E>
    void setDefaultExecutor(std::weak_ptr<E> exec) {
        setDomain(std::make_shared<detail::WeakExecutor>(std::move(exec)));
    }

    [[nodiscard]] slark::CancellationToken token() const noexcept {
        return core_->cancelToken;
    }

 private:
    friend struct AsyncScopeTestAccess;

    void setDomain(std::shared_ptr<IExecutor> exec) {
        std::shared_ptr<IExecutor> replaced;
        {
            std::lock_guard lock(core_->mutex);
            if (core_->spawned) {
                throw std::logic_error("AsyncScope::setDefaultExecutor after the first spawn");
            }
            replaced = std::exchange(core_->defaultExecutor, std::move(exec));
        }
        // Released outside the lock: destroying an owned executor joins its worker thread.
    }

    template <typename T>
    bool startChild(Task<T> task, slark::CancellationToken token) {
        // Caller errors: rejected before anything is registered, so the scope is not failed.
        if (!task.isValid() || task.getHandle().promise().hasStarted()) {
            throw std::logic_error("AsyncScope::spawn requires a valid Task that has not started");
        }
        const auto id = nextId_.fetch_add(1, std::memory_order_relaxed);
        auto wrapperTask = detail::scopeWrapper<T>(core_, id, std::move(task));
        wrapperTask.setCancellationToken(std::move(token));
        auto handle = wrapperTask.getHandle();
        auto frameControl = handle.promise().frameControl();
        std::shared_ptr<IExecutor> executor;
        {
            std::lock_guard lock(core_->mutex);
            core_->spawned = true;
            executor = core_->defaultExecutor;
            if (executor && !executor->isInline()) {
                wrapperTask.setExecutor(executor);
            }
            core_->children.emplace(id, std::move(wrapperTask));
        }

        if (executor && !executor->isInline()) {
            const bool accepted = executor->tryExecute([frameControl, handle]() noexcept {
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

    std::shared_ptr<detail::ScopeCore> core_;
    std::atomic<std::uint64_t> nextId_{0};
};

}  // namespace slark::coro
