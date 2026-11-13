// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "AsyncIOOperation.hpp"
#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "CoroutineTestSupport.h"
#include "Executor.h"
#include "Job.hpp"
#include "Task.hpp"
#include "Yield.hpp"

using namespace std::chrono_literals;

using slark::CancellationToken;
using slark::CancelledException;
using slark::CompletionAwaiter;
using slark::IOResult;
using slark::SerialExecutor;
using slark::Task;
using slark::TimerAwaiter;
using slark::coro::Job;
using slark::coro::launch;
using slark::test::ManualIO;
using slark::test::waitUntilDone;

namespace {

using Counter = std::shared_ptr<std::atomic<int>>;

Counter makeCounter() {
    return std::make_shared<std::atomic<int>>(0);
}

// True when the task ended by CancelledException. Non-cancel failures are not expected here.
template <typename T>
bool endedCancelled(Task<T>& task) {
    try {
        (void)task.result();
        return false;
    } catch (const CancelledException&) {
        return true;
    }
}

Task<void> timerThenMark(std::chrono::milliseconds delay, Counter ran) {
    co_await TimerAwaiter(delay);
    ran->fetch_add(1);
}

Task<void> yieldThenMark(Counter ran) {
    co_await slark::yield();
    ran->fetch_add(1);
}

Task<void> completionThenMark(std::shared_ptr<CompletionAwaiter<void>> gate, Counter ran) {
    co_await *gate;
    ran->fetch_add(1);
}

Task<void> joinThenMark(Job<void>* job, Counter ran) {
    co_await job->join();
    ran->fetch_add(1);
}

Task<void> awaitGate(std::shared_ptr<CompletionAwaiter<void>> gate) {
    co_await *gate;
}

Task<IOResult<std::string>> ioThenMark(std::shared_ptr<ManualIO> io, Counter ran) {
    auto outcome = co_await *io;
    ran->fetch_add(1);
    co_return outcome;
}

// Completes inside startOperation, before the coroutine would suspend.
class InlineIO final : public slark::AsyncIOOperationBase<InlineIO, void> {
protected:
    void startOperation() noexcept override {
        complete();
    }
};

Task<void> inlineIoThenMark(Counter ran) {
    (void)co_await InlineIO{};
    ran->fetch_add(1);
}

std::shared_ptr<SerialExecutor> stoppedExecutor() {
    auto executor = std::make_shared<SerialExecutor>();
    executor->requestStop();
    return executor;
}

}  // namespace

// Timer

TEST(SuspensionMatrixTest, TimerPreCancelEndsCancelledWithoutSuspending) {
    CancellationToken token;
    token.cancel();
    auto ran = makeCounter();
    auto task = timerThenMark(5000ms, ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone()) << "a token cancelled before suspension must not suspend";
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, TimerMidCancelEndsCancelled) {
    CancellationToken token;
    auto ran = makeCounter();
    auto task = timerThenMark(5000ms, ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, TimerZeroDelayCompletesWithoutSuspending) {
    auto ran = makeCounter();
    auto task = timerThenMark(0ms, ran);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_NO_THROW((void)task.result());
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, TimerDestroyedWhileSuspendedNeverResumes) {
    auto ran = makeCounter();
    {
        auto task = timerThenMark(20ms, ran);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }
    std::this_thread::sleep_for(80ms);
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, TimerRefusedDispatchEndsCancelled) {
    auto ran = makeCounter();
    auto task = timerThenMark(5ms, ran);
    task.setExecutor(stoppedExecutor());
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_TRUE(endedCancelled(task)) << "a refused resume is a cancelled wait";
    EXPECT_EQ(ran->load(), 0);
}

// Yield: always suspends, and its zero-delay tick may precede any cancel or executor binding,
// so only the racing outcome is deterministic.

TEST(SuspensionMatrixTest, YieldPreCancelEndsCancelledWithoutSuspending) {
    CancellationToken token;
    token.cancel();
    auto ran = makeCounter();
    auto task = yieldThenMark(ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, YieldCancelRacingItsTickResolvesToExactlyOneOutcome) {
    for (int round = 0; round < 50; ++round) {
        CancellationToken token;
        auto ran = makeCounter();
        auto task = yieldThenMark(ran);
        task.setCancellationToken(token);
        task.resume();
        token.cancel();
        ASSERT_TRUE(waitUntilDone(task)) << "round " << round;
        const int cancelled = endedCancelled(task) ? 1 : 0;
        EXPECT_EQ(ran->load() + cancelled, 1) << "round " << round;
    }
}

// CompletionAwaiter

TEST(SuspensionMatrixTest, CompletionPreCancelEndsCancelledWithoutSuspending) {
    CancellationToken token;
    token.cancel();
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto ran = makeCounter();
    auto task = completionThenMark(gate, ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, CompletionMidCancelEndsCancelledAndLaterCompletionIsInert) {
    CancellationToken token;
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto ran = makeCounter();
    auto task = completionThenMark(gate, ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    gate->complete();
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, CompletionAlreadyCompletedDoesNotSuspend) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    gate->complete();
    auto ran = makeCounter();
    auto task = completionThenMark(gate, ran);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_NO_THROW((void)task.result());
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, CompletionDestroyedWhileSuspendedIsNotResumed) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto ran = makeCounter();
    {
        auto task = completionThenMark(gate, ran);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }
    gate->complete();
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, CompletionRefusedDispatchEndsCancelled) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto ran = makeCounter();
    auto task = completionThenMark(gate, ran);
    task.setExecutor(stoppedExecutor());
    task.resume();
    ASSERT_FALSE(task.isDone());

    gate->complete();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

// Job::join

TEST(SuspensionMatrixTest, JoinPreCancelEndsCancelledAndLeavesTheJobRunning) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(awaitGate(gate));
    CancellationToken token;
    token.cancel();
    auto ran = makeCounter();
    auto joiner = joinThenMark(&job, ran);
    joiner.setCancellationToken(token);
    joiner.resume();
    ASSERT_TRUE(joiner.isDone());
    EXPECT_TRUE(endedCancelled(joiner));
    EXPECT_FALSE(job.isDone()) << "cancelling a joiner must not cancel the job";
    gate->complete();
    EXPECT_TRUE(job.waitFor(3s));
}

TEST(SuspensionMatrixTest, JoinMidCancelEndsCancelledAndLeavesTheJobRunning) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(awaitGate(gate));
    CancellationToken token;
    auto ran = makeCounter();
    auto joiner = joinThenMark(&job, ran);
    joiner.setCancellationToken(token);
    joiner.resume();
    ASSERT_FALSE(joiner.isDone());

    token.cancel();
    ASSERT_TRUE(joiner.isDone());
    EXPECT_TRUE(endedCancelled(joiner));
    EXPECT_FALSE(job.isDone());
    gate->complete();
    EXPECT_TRUE(job.waitFor(3s));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, JoinAlreadySettledDoesNotSuspend) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(awaitGate(gate));
    gate->complete();
    ASSERT_TRUE(job.waitFor(3s));
    auto ran = makeCounter();
    auto joiner = joinThenMark(&job, ran);
    joiner.resume();
    ASSERT_TRUE(joiner.isDone());
    EXPECT_NO_THROW((void)joiner.result());
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, JoinReleasedByDestroyingTheJobEndsNormally) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto ran = makeCounter();
    Task<void> joiner;
    {
        auto job = launch(awaitGate(gate));
        joiner = joinThenMark(&job, ran);
        joiner.resume();
        ASSERT_FALSE(joiner.isDone());
    }
    ASSERT_TRUE(joiner.isDone()) << "the destroyed job releases its joiners";
    EXPECT_NO_THROW((void)joiner.result()) << "a released join is not a cancelled join";
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, JoinDestroyedWhileSuspendedIsNotResumed) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(awaitGate(gate));
    auto ran = makeCounter();
    {
        auto joiner = joinThenMark(&job, ran);
        joiner.resume();
        ASSERT_FALSE(joiner.isDone());
    }
    gate->complete();
    EXPECT_TRUE(job.waitFor(3s));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, JoinRefusedDispatchEndsCancelled) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(awaitGate(gate));
    auto ran = makeCounter();
    auto joiner = joinThenMark(&job, ran);
    joiner.setExecutor(stoppedExecutor());
    joiner.resume();
    ASSERT_FALSE(joiner.isDone());

    gate->complete();
    ASSERT_TRUE(waitUntilDone(joiner));
    EXPECT_TRUE(endedCancelled(joiner));
    EXPECT_EQ(ran->load(), 0);
}

// AsyncIOOperationBase (ManualIO has no event source of its own; fd readiness and terminal
// notification are covered by the http contract matrix)

TEST(SuspensionMatrixTest, IoPreCancelEndsCancelledWithoutSuspending) {
    CancellationToken token;
    token.cancel();
    auto io = std::make_shared<ManualIO>();
    auto ran = makeCounter();
    auto task = ioThenMark(io, ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, IoMidCancelEndsCancelledAndLaterCompletionIsInert) {
    CancellationToken token;
    auto io = std::make_shared<ManualIO>();
    auto ran = makeCounter();
    auto task = ioThenMark(io, ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    io->finish("late");
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, IoCompletedInStartDoesNotSuspend) {
    auto ran = makeCounter();
    auto task = inlineIoThenMark(ran);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_NO_THROW((void)task.result());
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, IoDestroyedWhileSuspendedIsNotResumed) {
    auto io = std::make_shared<ManualIO>();
    auto ran = makeCounter();
    {
        auto task = ioThenMark(io, ran);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }
    io->finish("late");
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, IoRefusedDispatchEndsCancelled) {
    auto io = std::make_shared<ManualIO>();
    auto ran = makeCounter();
    auto task = ioThenMark(io, ran);
    task.setExecutor(stoppedExecutor());
    task.resume();
    ASSERT_FALSE(task.isDone());

    io->finish("value");
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_TRUE(endedCancelled(task)) << "a refused resume discards the completed result";
    EXPECT_EQ(ran->load(), 0);
}
