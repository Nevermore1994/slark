//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <chrono>
#include <coroutine>
#include <memory>
#include <thread>

#include "Awaitable.hpp"
#include "CoroutineScheduler.hpp"
#include "CoroutineTestSupport.h"
#include "Executor.h"
#include "Task.hpp"

using namespace std::chrono_literals;
using slark::CompletionAwaiter;
using slark::CoroutineScheduler;
using slark::SerialExecutor;
using slark::Task;
using slark::test::waitUntilDone;

namespace {

Task<void> awaitGate(std::shared_ptr<CompletionAwaiter<void>> gate) {
    co_await *gate;
}

}  // namespace

TEST(ResumeMetricsTest, ResumesHandedToAnExecutorAreCounted) {
    auto& metrics = CoroutineScheduler::instance().resumeMetrics();
    const auto before = metrics.dispatched.load();
    auto executor = std::make_shared<SerialExecutor>();
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto task = awaitGate(gate);
    task.setExecutor(executor);
    task.resume();
    ASSERT_FALSE(task.isDone());

    gate->complete();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_GE(metrics.dispatched.load() - before, 1u);
}

TEST(ResumeMetricsTest, ResumesWithoutAnExecutorAreCountedAsInline) {
    auto& metrics = CoroutineScheduler::instance().resumeMetrics();
    const auto before = metrics.inlined.load();
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto task = awaitGate(gate);
    task.resume();
    ASSERT_FALSE(task.isDone());

    gate->complete();
    EXPECT_TRUE(task.isDone());
    EXPECT_GE(metrics.inlined.load() - before, 1u);
}

TEST(ResumeMetricsTest, RefusedResumesAreCountedAndStillRunOnTheFallback) {
    auto& metrics = CoroutineScheduler::instance().resumeMetrics();
    const auto before = metrics.refused.load();
    auto executor = std::make_shared<SerialExecutor>();
    executor->requestStop();
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto task = awaitGate(gate);
    task.setExecutor(executor);
    task.resume();
    ASSERT_FALSE(task.isDone());

    gate->complete();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_GE(metrics.refused.load() - before, 1u);
}

TEST(ResumeMetricsTest, TimeWaitedInTheExecutorQueueIsRecorded) {
    auto& metrics = CoroutineScheduler::instance().resumeMetrics();
    auto executor = std::make_shared<SerialExecutor>();
    executor->execute([] {
        std::this_thread::sleep_for(30ms);
    });
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto task = awaitGate(gate);
    task.setExecutor(executor);
    task.resume();
    gate->complete();

    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_GE(metrics.queueLatencyUsMax.load(), 10000u)
        << "the resume waited behind a 30 ms task, so its queue latency must be at least 10 ms";
    EXPECT_GE(metrics.queueLatencyUsTotal.load(), 10000u);
}

TEST(ResumeMetricsTest, PendingCountsAcceptedResumesUntilTheyStart) {
    auto& metrics = CoroutineScheduler::instance().resumeMetrics();
    const auto baseline = metrics.pending.load();
    auto executor = std::make_shared<SerialExecutor>();
    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> release{false};
    executor->execute([&] {
        blockerStarted.store(true);
        while (!release.load()) {
            std::this_thread::sleep_for(1ms);
        }
    });
    while (!blockerStarted.load()) {
        std::this_thread::sleep_for(1ms);
    }

    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto task = awaitGate(gate);
    task.setExecutor(executor);
    task.resume();
    gate->complete();
    EXPECT_EQ(metrics.pending.load(), baseline + 1);

    release.store(true);
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_EQ(metrics.pending.load(), baseline);
}

namespace {

struct ResumeOwnFrame {
    bool* refused;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        *refused = !handle.promise().frameControl()->resume(handle);
        return false;
    }

    void await_resume() const noexcept {}
};

Task<void> resumeItselfWhileRunning(bool* refused) {
    co_await ResumeOwnFrame{refused};
}

}  // namespace

TEST(ResumeMetricsTest, ResumeOfAFrameThatIsStillRunningIsRefusedAndCounted) {
    auto& metrics = CoroutineScheduler::instance().resumeMetrics();
    const auto before = metrics.conflicts.load();
    bool refused = false;
    auto task = resumeItselfWhileRunning(&refused);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(refused) << "a frame must not be resumed while its body is still running";
    EXPECT_EQ(metrics.conflicts.load() - before, 1u);
    EXPECT_EQ(task.getHandle().promise().frameControl()->executionConflicts(), 1u);
}
