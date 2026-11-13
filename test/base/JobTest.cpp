// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "CoroutineControl.hpp"
#include "Executor.h"
#include "Job.hpp"
#include "Task.hpp"

using namespace std::chrono_literals;

using slark::CancellationToken;
using slark::CancelledException;
using slark::CompletionAwaiter;
using slark::SerialExecutor;
using slark::Task;
using slark::coro::Job;
using slark::coro::LaunchOptions;
using slark::coro::launch;
using slark::coro::makeJob;

namespace {

using Counter = std::shared_ptr<std::atomic<int>>;

Counter makeCounter() {
    return std::make_shared<std::atomic<int>>(0);
}

Task<void> awaitGate(std::shared_ptr<CompletionAwaiter<void>> gate, Counter finished) {
    co_await *gate;
    finished->fetch_add(1);
}

Task<int> valueAfterGate(std::shared_ptr<CompletionAwaiter<void>> gate, int value) {
    co_await *gate;
    co_return value;
}

Task<void> failAfterGate(std::shared_ptr<CompletionAwaiter<void>> gate) {
    co_await *gate;
    throw std::runtime_error("boom");
}

Task<void> markRan(Counter ran) {
    ran->fetch_add(1);
    co_return;
}

Task<void> recordThread(std::atomic<std::thread::id>* out) {
    out->store(std::this_thread::get_id());
    co_return;
}

// Records how the join ended: 1 when it returned, 2 when the joiner's own token cancelled it.
Task<void> joinAndRecord(Job<void>* job, Counter outcome) {
    try {
        co_await job->join();
        outcome->store(1);
    } catch (const CancelledException&) {
        outcome->store(2);
    }
}

// Suspends without a cancellation binding, so only destroying the frame can end it.
struct UncancellableGate {
    std::shared_ptr<std::shared_ptr<slark::ResumeGate>> out;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        slark::releaseTransferLeases(handle.promise());
        slark::markExternalAwait(handle.promise());
        *out = slark::makeResumeGate(handle);
        return (*out)->arm();
    }

    void await_resume() const noexcept {}
};

Task<void> parkUncancellable(std::shared_ptr<std::shared_ptr<slark::ResumeGate>> out,
                             Counter finished) {
    co_await UncancellableGate{out};
    finished->fetch_add(1);
}

std::thread::id threadOf(slark::IExecutor& executor) {
    std::promise<std::thread::id> probe;
    auto id = probe.get_future();
    executor.execute([p = std::move(probe)]() mutable { p.set_value(std::this_thread::get_id()); });
    return id.get();
}

}  // namespace

TEST(JobTest, JoinReturnsNormallyWhenTheBodySucceeds) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(awaitGate(gate, makeCounter()));
    auto outcome = makeCounter();
    auto joiner = joinAndRecord(&job, outcome);
    joiner.resume();
    ASSERT_FALSE(joiner.isDone());

    gate->complete();
    ASSERT_TRUE(job.waitFor(3s));
    EXPECT_TRUE(joiner.isDone());
    EXPECT_EQ(outcome->load(), 1);
}

TEST(JobTest, ResultCarriesTheValueOfTheBody) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(valueAfterGate(gate, 7));
    gate->complete();
    ASSERT_TRUE(job.waitFor(3s));
    EXPECT_EQ(job.result(), 7);
}

TEST(JobTest, FailureIsRethrownByResultButNotByJoin) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(failAfterGate(gate));
    auto outcome = makeCounter();
    auto joiner = joinAndRecord(&job, outcome);
    joiner.resume();

    gate->complete();
    ASSERT_TRUE(job.waitFor(3s));
    EXPECT_TRUE(joiner.isDone());
    EXPECT_EQ(outcome->load(), 1) << "join must not rethrow the body's failure";
    EXPECT_THROW(job.result(), std::runtime_error);
}

TEST(JobTest, CancelWakesTheBodyAndSettlesItAsCancelled) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto finished = makeCounter();
    auto job = launch(awaitGate(gate, finished));
    ASSERT_FALSE(job.isDone());

    job.cancel();
    ASSERT_TRUE(job.waitFor(3s));
    EXPECT_THROW(job.result(), CancelledException);
    EXPECT_EQ(finished->load(), 0);
}

TEST(JobTest, WaitForReportsTimeoutWhileTheBodyRuns) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(awaitGate(gate, makeCounter()));
    EXPECT_FALSE(job.waitFor(20ms));
    EXPECT_FALSE(job.isDone());

    gate->complete();
    EXPECT_TRUE(job.waitFor(3s));
}

TEST(JobTest, WaitForIsWokenByCompletionOnAnotherThread) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(awaitGate(gate, makeCounter()));
    std::thread completer([gate] {
        std::this_thread::sleep_for(20ms);
        gate->complete();
    });

    const auto start = std::chrono::steady_clock::now();
    const bool settled = job.waitFor(5s);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    completer.join();

    EXPECT_TRUE(settled);
    EXPECT_LT(elapsed, 2s) << "completion must wake waitFor instead of running out the timeout";
}

TEST(JobTest, DestroyingAStartedJobCancelsItsBodyWithoutWaiting) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto finished = makeCounter();
    const auto start = std::chrono::steady_clock::now();
    {
        auto job = launch(awaitGate(gate, finished));
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s)
        << "the destructor must not wait for the body";
    EXPECT_EQ(finished->load(), 0);

    gate->complete();
    EXPECT_EQ(finished->load(), 0) << "the destroyed frame must not be resumed";
}

TEST(JobTest, DestroyingAnUnstartedJobNeverRunsItAndReleasesItsJoiners) {
    auto ran = makeCounter();
    auto outcome = makeCounter();
    Task<void> joiner;
    {
        auto job = makeJob(markRan(ran));
        joiner = joinAndRecord(&job, outcome);
        joiner.resume();
        ASSERT_FALSE(joiner.isDone());
    }
    EXPECT_TRUE(joiner.isDone()) << "destroying the job settles it, so joiners are released";
    EXPECT_EQ(outcome->load(), 1);
    EXPECT_EQ(ran->load(), 0);
}

TEST(JobTest, DestroyingAJobWhoseBodyIgnoresCancelReleasesItsJoiners) {
    auto out = std::make_shared<std::shared_ptr<slark::ResumeGate>>();
    auto finished = makeCounter();
    auto outcome = makeCounter();
    Task<void> joiner;
    {
        auto job = launch(parkUncancellable(out, finished));
        joiner = joinAndRecord(&job, outcome);
        joiner.resume();
        ASSERT_FALSE(joiner.isDone());
    }
    EXPECT_TRUE(joiner.isDone()) << "destruction releases joiners without waiting for the body";
    EXPECT_EQ(outcome->load(), 1);
    EXPECT_EQ(finished->load(), 0);

    // The uncancellable child is detached, not destroyed: it runs to completion when woken,
    // as an AsyncScope child that ignores cancellation does.
    ASSERT_TRUE(*out);
    (*out)->signal();
    EXPECT_EQ(finished->load(), 1);
}

TEST(JobTest, MovedJobTransfersOwnershipAndTheSourceIsEmpty) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto finished = makeCounter();
    auto job = launch(awaitGate(gate, finished));
    Job<void> moved = std::move(job);

    EXPECT_FALSE(job.isValid());
    EXPECT_TRUE(job.isDone()) << "an empty Job reports done";
    job.cancel();
    EXPECT_FALSE(moved.isDone());

    moved.cancel();
    EXPECT_TRUE(moved.waitFor(3s));
    EXPECT_EQ(finished->load(), 0);
}

TEST(JobTest, MoveAssignmentCancelsThePreviousJob) {
    auto gateA = std::make_shared<CompletionAwaiter<void>>();
    auto gateB = std::make_shared<CompletionAwaiter<void>>();
    auto finishedA = makeCounter();
    auto finishedB = makeCounter();
    auto first = launch(awaitGate(gateA, finishedA));
    auto second = launch(awaitGate(gateB, finishedB));

    first = std::move(second);
    EXPECT_FALSE(second.isValid());
    EXPECT_TRUE(first.isValid());

    gateB->complete();
    EXPECT_TRUE(first.waitFor(3s));
    EXPECT_EQ(finishedB->load(), 1);
    EXPECT_EQ(finishedA->load(), 0) << "the replaced job was cancelled, not completed";
}

TEST(JobTest, ExecutorRunsTheBodyOnItsDomain) {
    auto executor = std::make_shared<SerialExecutor>();
    const auto worker = threadOf(*executor);
    std::atomic<std::thread::id> ranOn{};
    auto job = launch(recordThread(&ranOn), LaunchOptions{.token = {}, .executor = executor});
    ASSERT_TRUE(job.waitFor(3s));
    EXPECT_EQ(ranOn.load(), worker);
}

TEST(JobTest, RefusedExecutorSettlesTheJobAsCancelledAndNeverRunsIt) {
    auto executor = std::make_shared<SerialExecutor>();
    executor->requestStop();
    auto ran = makeCounter();
    auto job = launch(markRan(ran), LaunchOptions{.token = {}, .executor = executor});
    EXPECT_TRUE(job.isDone()) << "a refused dispatch settles the job before launch returns";
    EXPECT_THROW(job.result(), CancelledException);
    EXPECT_EQ(ran->load(), 0);
}

TEST(JobTest, CancellingAJoinerStopsWaitingWithoutCancellingTheJob) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto finished = makeCounter();
    auto job = launch(awaitGate(gate, finished));
    CancellationToken joinerToken;
    auto outcome = makeCounter();
    Task<void> joiner = joinAndRecord(&job, outcome);
    joiner.setCancellationToken(joinerToken);
    joiner.resume();
    ASSERT_FALSE(joiner.isDone());

    joinerToken.cancel();
    EXPECT_TRUE(joiner.isDone());
    EXPECT_EQ(outcome->load(), 2) << "the joiner observes its own cancellation";
    EXPECT_FALSE(job.isDone()) << "cancelling a joiner must not cancel the job";

    gate->complete();
    EXPECT_TRUE(job.waitFor(3s));
    EXPECT_EQ(finished->load(), 1);
}

TEST(JobTest, EveryJoinerWakesWhenTheJobSettles) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(awaitGate(gate, makeCounter()));
    std::vector<Counter> outcomes;
    std::vector<Task<void>> joiners;
    for (int i = 0; i < 3; ++i) {
        outcomes.push_back(makeCounter());
        joiners.push_back(joinAndRecord(&job, outcomes.back()));
    }
    for (auto& joiner : joiners) {
        joiner.resume();
        ASSERT_FALSE(joiner.isDone());
    }

    gate->complete();
    for (std::size_t i = 0; i < joiners.size(); ++i) {
        EXPECT_TRUE(joiners[i].isDone()) << "joiner " << i;
        EXPECT_EQ(outcomes[i]->load(), 1) << "joiner " << i;
    }
}

TEST(JobTest, MakeJobRejectsAnEmptyOrStartedTask) {
    EXPECT_THROW((void)makeJob(Task<void>{}), std::logic_error);

    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto finished = makeCounter();
    Task<void> started = awaitGate(gate, finished);
    started.resume();
    EXPECT_THROW((void)makeJob(std::move(started)), std::logic_error);

    gate->complete();
    EXPECT_EQ(finished->load(), 0) << "the rejected task was released and must not run on";
}

TEST(JobTest, ResultBeforeSettlingThrows) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto job = launch(valueAfterGate(gate, 1));
    EXPECT_THROW((void)job.result(), std::runtime_error);

    gate->complete();
    ASSERT_TRUE(job.waitFor(3s));
    EXPECT_EQ(job.result(), 1);
}

TEST(JobTest, StartIsIdempotent) {
    auto ran = makeCounter();
    auto job = makeJob(markRan(ran));
    EXPECT_FALSE(job.isDone());
    job.start();
    job.start();
    ASSERT_TRUE(job.waitFor(3s));
    EXPECT_EQ(ran->load(), 1);
}

TEST(JobTest, CallerTokenPassedToLaunchCancelsTheBody) {
    CancellationToken outer;
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto finished = makeCounter();
    auto job = launch(awaitGate(gate, finished), LaunchOptions{.token = outer});

    outer.cancel();
    ASSERT_TRUE(job.waitFor(3s));
    EXPECT_THROW(job.result(), CancelledException);
    EXPECT_EQ(finished->load(), 0);
}

TEST(JobTest, EmptyJobIsDoneAndCannotBeJoined) {
    Job<void> empty;
    EXPECT_FALSE(empty.isValid());
    EXPECT_TRUE(empty.isDone());
    EXPECT_TRUE(empty.waitFor(0ms));
    EXPECT_THROW((void)empty.join(), std::logic_error);
    EXPECT_THROW(empty.result(), std::runtime_error);
}

TEST(JobTest, CompletionRacingCancelSettlesExactlyOnce) {
    for (int round = 0; round < 200; ++round) {
        auto gate = std::make_shared<CompletionAwaiter<void>>();
        auto finished = makeCounter();
        auto job = launch(awaitGate(gate, finished));
        std::thread completer([gate] { gate->complete(); });
        std::thread canceller([&job] { job.cancel(); });
        completer.join();
        canceller.join();

        ASSERT_TRUE(job.waitFor(3s)) << "round " << round;
        EXPECT_LE(finished->load(), 1) << "round " << round;
    }
}

TEST(JobTest, DestroyRacingCompletionNeverTouchesAFreedFrame) {
    for (int round = 0; round < 200; ++round) {
        auto gate = std::make_shared<CompletionAwaiter<void>>();
        auto finished = makeCounter();
        auto job = std::make_unique<Job<void>>(launch(awaitGate(gate, finished)));
        std::thread completer([gate] { gate->complete(); });
        job.reset();
        completer.join();
        EXPECT_LE(finished->load(), 1) << "round " << round;
    }
}
