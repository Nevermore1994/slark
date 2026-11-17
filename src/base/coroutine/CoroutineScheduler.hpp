// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

#include "Executor.h"
#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"
#include "ThreadPool.hpp"
#include "Timer.h"
#include "TimerManager.h"

namespace slark {

class FallbackExecutor final : public IExecutor {
 public:
    void execute(MoveOnlyFunction<void()> task) noexcept override {
        (void)tryExecute(std::move(task));
    }

    bool tryExecute(MoveOnlyFunction<void()> task) noexcept override {
        return pool_.postJob(std::move(task)).has_value();
    }

    bool isInline() const noexcept override {
        return false;
    }

 private:
    ThreadPool pool_{ThreadPoolConfig{.threadCount = 4}};
};

struct ResumeMetrics {
    std::atomic<uint64_t> inlined{0};
    std::atomic<uint64_t> dispatched{0};
    std::atomic<uint64_t> refused{0};
    std::atomic<uint64_t> pending{0};
    std::atomic<uint64_t> conflicts{0};
    std::atomic<uint64_t> queueLatencyUsTotal{0};
    std::atomic<uint64_t> queueLatencyUsMax{0};

    void recordQueueLatency(std::chrono::steady_clock::time_point enqueuedAt) noexcept {
        const auto waited = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - enqueuedAt)
                                .count();
        const auto us = waited > 0 ? static_cast<uint64_t>(waited) : uint64_t{0};
        queueLatencyUsTotal.fetch_add(us, std::memory_order_relaxed);
        auto seen = queueLatencyUsMax.load(std::memory_order_relaxed);
        while (us > seen &&
               !queueLatencyUsMax.compare_exchange_weak(seen, us, std::memory_order_relaxed)) {
        }
    }
};

class CoroutineScheduler : public NonCopyable {
 public:
    static CoroutineScheduler& instance() noexcept {
        static CoroutineScheduler scheduler;
        return scheduler;
    }

    IExecutor& fallbackExecutor() noexcept {
        static auto* executor = new FallbackExecutor();
        return *executor;
    }

    ResumeMetrics& resumeMetrics() noexcept {
        return resumeMetrics_;
    }

    template <std::invocable Func>
    TimerId scheduleCallback(std::chrono::milliseconds delay, Func&& callback) {
        return TimerManager::shareInstance().runAfter(delay, std::forward<Func>(callback));
    }

    void cancel(TimerId timerId) noexcept {
        if (timerId.isValid()) {
            TimerManager::shareInstance().cancel(timerId);
        }
    }

 private:
    CoroutineScheduler() = default;
    ~CoroutineScheduler() = default;

    ResumeMetrics resumeMetrics_;
};

}  // namespace slark
