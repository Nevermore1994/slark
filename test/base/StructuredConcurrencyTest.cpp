// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

#include "Awaitable.hpp"
#include "CoScope.hpp"
#include "CoWhenAll.hpp"
#include "CoroutineTestSupport.h"
#include "Error.h"
#include "Executor.h"
#include "Scheduler.h"
#include "Task.hpp"

using namespace std::chrono_literals;

using slark::CancellationToken;
using slark::CancelledException;
using slark::CompletionAwaiter;
using slark::IExecutor;
using slark::SerialExecutor;
using slark::Task;
using slark::coro::AsyncScope;
using slark::coro::when_all;
using slark::coro::when_any;
using slark::test::waitUntilDone;

namespace {

Task<void> awaitOne(std::shared_ptr<CompletionAwaiter<void>> awaiter) {
    co_await *awaiter;
}

Task<void> markRan(std::shared_ptr<std::atomic<bool>> ran) {
    ran->store(true);
    co_return;
}

Task<void> failAfter(std::shared_ptr<CompletionAwaiter<void>> gate) {
    co_await *gate;
    throw std::runtime_error("boom");
}

Task<void> recordCancellation(std::shared_ptr<CompletionAwaiter<void>> gate,
                              std::shared_ptr<std::atomic<bool>> observed) {
    try {
        co_await *gate;
    } catch (const CancelledException&) {
        observed->store(true);
        throw;
    }
}

Task<void> parentFailingSibling(std::shared_ptr<CompletionAwaiter<void>> failing,
                                std::shared_ptr<CompletionAwaiter<void>> sibling,
                                std::shared_ptr<std::atomic<bool>> observed) {
    co_await when_all(failAfter(std::move(failing)),
                      recordCancellation(std::move(sibling), std::move(observed)));
}

Task<void> holdPayload(std::shared_ptr<CompletionAwaiter<void>> gate,
                       std::shared_ptr<std::atomic<bool>> observed,
                       std::shared_ptr<int> payload) {
    try {
        co_await *gate;
    } catch (const CancelledException&) {
        observed->store(true);
        throw;
    }
    (void)payload;
}

Task<void> parentHoldingChild(std::shared_ptr<CompletionAwaiter<void>> gate,
                              std::shared_ptr<std::atomic<bool>> observed,
                              std::shared_ptr<int> payload) {
    co_await when_all(holdPayload(std::move(gate), std::move(observed), std::move(payload)));
}

Task<void> parentWhenAllOn(IExecutor& executor, std::shared_ptr<std::atomic<bool>> ran) {
    co_await when_all(executor, markRan(std::move(ran)));
}

Task<int> parentWhenAnyOn(AsyncScope& scope, std::shared_ptr<std::atomic<bool>> ran) {
    auto result = co_await when_any(scope, markRan(ran), markRan(ran));
    co_return static_cast<int>(result.winnerIndex);
}

}  // namespace

TEST(StructuredConcurrencyTest, RefusedResumeIsCancelledAndUnwindsOnFallback) {
    auto executor = std::make_shared<SerialExecutor>();
    auto awaiter = std::make_shared<CompletionAwaiter<void>>();
    auto task = awaitOne(awaiter);
    task.setExecutor(executor);
    task.resume();
    ASSERT_FALSE(task.isDone());

    executor->requestStop();
    awaiter->complete();

    ASSERT_TRUE(waitUntilDone(task)) << "a refused resume must still unwind the frame";
    EXPECT_THROW(task.result(), CancelledException);
}

namespace {

struct DestroyThreadRecorder {
    std::thread::id* out;

    ~DestroyThreadRecorder() {
        *out = std::this_thread::get_id();
    }
};

Task<void> awaitWithRecordedLocal(std::shared_ptr<CompletionAwaiter<void>> awaiter,
                                  std::thread::id* destroyedOn) {
    DestroyThreadRecorder recorder{destroyedOn};
    co_await *awaiter;
}

}  // namespace

TEST(StructuredConcurrencyTest, RefusedResumeDestroysLocalsOffTheCallerThread) {
    auto executor = std::make_shared<SerialExecutor>();
    auto awaiter = std::make_shared<CompletionAwaiter<void>>();
    auto destroyedOn = std::make_shared<std::thread::id>();
    auto task = awaitWithRecordedLocal(awaiter, destroyedOn.get());
    task.setExecutor(executor);
    task.resume();
    ASSERT_FALSE(task.isDone());

    executor->requestStop();
    const auto callerThread = std::this_thread::get_id();
    awaiter->complete();

    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_NE(*destroyedOn, std::thread::id{}) << "the frame's locals must be destroyed";
    EXPECT_NE(*destroyedOn, callerThread) << "refused resume must unwind off the caller";
}

TEST(StructuredConcurrencyTest, RefusedResumeOnStoppedThreadSchedulerUnwindsOnFallback) {
    auto scheduler = std::make_shared<slark::ThreadScheduler>("refusedResume");
    auto awaiter = std::make_shared<CompletionAwaiter<void>>();
    auto task = awaitOne(awaiter);
    task.setExecutor(scheduler);
    task.resume();
    ASSERT_FALSE(task.isDone());

    scheduler->stop();
    awaiter->complete();

    ASSERT_TRUE(waitUntilDone(task)) << "a resume refused by a stopped scheduler must still unwind";
    EXPECT_THROW(task.result(), CancelledException);
}

namespace {

Task<void> raceFailingTimer(std::chrono::milliseconds delay) {
    co_await slark::TimerAwaiter(delay);
    throw std::runtime_error("boom");
}

Task<void> raceSiblingWaiter(std::shared_ptr<CompletionAwaiter<int>> gate,
                             std::atomic<int>* finished) {
    try {
        co_await *gate;
    } catch (...) {
        finished->fetch_add(1);
        throw;
    }
    finished->fetch_add(1);
}

Task<void> raceWhenAll(Task<void> failing, Task<void> sibling) {
    co_await when_all(std::move(failing), std::move(sibling));
}

}  // namespace

TEST(StructuredConcurrencyTest, FailureRacingCompletionRethrowsOnlyAfterEveryChildFinishes) {
    for (int round = 0; round < 20; ++round) {
        auto gate = std::make_shared<CompletionAwaiter<int>>();
        std::atomic<int> finished{0};
        auto root = raceWhenAll(raceFailingTimer(1ms), raceSiblingWaiter(gate, &finished));
        root.resume();
        std::thread completer([gate] {
            gate->complete(1);
        });
        completer.join();
        ASSERT_TRUE(waitUntilDone(root)) << "round " << round;
        EXPECT_THROW(root.result(), std::runtime_error) << "round " << round;
        EXPECT_EQ(finished.load(), 1) << "round " << round;
    }
}

TEST(StructuredConcurrencyTest, RefusedWhenAllChildIsCancelledWithoutRunning) {
    auto executor = std::make_shared<SerialExecutor>();
    executor->requestStop();
    auto ran = std::make_shared<std::atomic<bool>>(false);
    auto parent = parentWhenAllOn(*executor, ran);

    parent.resume();

    ASSERT_TRUE(parent.isDone()) << "a refused child must not leave the parent suspended";
    EXPECT_FALSE(ran->load());
    EXPECT_THROW(parent.result(), CancelledException);
}

TEST(StructuredConcurrencyTest, WhenAnyWithOnlyRefusedChildrenIsCancelled) {
    auto executor = std::make_shared<SerialExecutor>();
    executor->requestStop();
    AsyncScope scope;
    scope.setDefaultExecutor(*executor);
    auto ran = std::make_shared<std::atomic<bool>>(false);
    auto parent = parentWhenAnyOn(scope, ran);

    parent.resume();

    ASSERT_TRUE(parent.isDone()) << "all children refused: the parent must not stay suspended";
    EXPECT_FALSE(ran->load());
    EXPECT_THROW(parent.result(), CancelledException);
}

TEST(StructuredConcurrencyTest, FailingChildCancelsSiblingsAndRethrowsRootFailure) {
    auto failing = std::make_shared<CompletionAwaiter<void>>();
    auto sibling = std::make_shared<CompletionAwaiter<void>>();
    auto observed = std::make_shared<std::atomic<bool>>(false);
    auto parent = parentFailingSibling(failing, sibling, observed);
    parent.resume();
    ASSERT_FALSE(parent.isDone());

    failing->complete();

    ASSERT_TRUE(waitUntilDone(parent)) << "the failing child must cancel the parked sibling";
    EXPECT_TRUE(observed->load());
    try {
        (void)parent.result();
        ADD_FAILURE() << "the parent must rethrow the child failure";
    } catch (const CancelledException&) {
        ADD_FAILURE() << "sibling cancellation must not mask the root failure";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "boom");
    }
}

TEST(StructuredConcurrencyTest, DestroyingWhenAllParentCancelsItsChildren) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto observed = std::make_shared<std::atomic<bool>>(false);
    auto payload = std::make_shared<int>(1);
    std::weak_ptr<int> weak = payload;
    {
        auto parent = parentHoldingChild(gate, observed, payload);
        payload.reset();
        parent.resume();
        ASSERT_FALSE(parent.isDone());
    }

    EXPECT_TRUE(observed->load()) << "the child must be cancelled, not left running orphaned";
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!weak.expired() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_TRUE(weak.expired()) << "the cancelled child's frame must be released";
}

namespace {

Task<void> ownerJoins(AsyncScope& scope, std::shared_ptr<std::atomic<bool>> joined) {
    co_await scope.join();
    joined->store(true);
}

Task<void> ownerSpawnsOnExecutor(IExecutor* executor, std::shared_ptr<std::atomic<bool>> childRan) {
    AsyncScope scope;
    scope.setDefaultExecutor(*executor);
    scope.spawn(markRan(std::move(childRan)));
    co_return;
}

}  // namespace

TEST(StructuredConcurrencyTest, ScopeDestroyedOnItsOwnExecutorDoesNotDeadlock) {
    auto executor = std::make_shared<SerialExecutor>();
    auto childRan = std::make_shared<std::atomic<bool>>(false);
    auto ownerFinished = std::make_shared<std::atomic<bool>>(false);
    auto owner = std::make_shared<Task<void>>(ownerSpawnsOnExecutor(executor.get(), childRan));
    executor->execute([owner, ownerFinished]() {
        owner->resume();
        ownerFinished->store(true);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!(ownerFinished->load() && childRan->load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(ownerFinished->load()) << "destroying a scope must not wait for its own executor";
    EXPECT_TRUE(childRan->load()) << "a detached child still runs on its executor";
}

TEST(StructuredConcurrencyTest, JoinSuspendsOwnerUntilChildrenFinish) {
    AsyncScope scope;
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    scope.spawn(awaitOne(gate));
    auto joined = std::make_shared<std::atomic<bool>>(false);
    auto owner = ownerJoins(scope, joined);

    owner.resume();
    ASSERT_FALSE(owner.isDone()) << "join must suspend while a child is still running";
    EXPECT_FALSE(joined->load());

    gate->complete();
    ASSERT_TRUE(waitUntilDone(owner));
    EXPECT_TRUE(joined->load());
}

TEST(StructuredConcurrencyTest, CancelledJoinStopsWaitingWithoutCancellingChildren) {
    AsyncScope scope;
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto childCancelled = std::make_shared<std::atomic<bool>>(false);
    scope.spawn(recordCancellation(gate, childCancelled));
    auto joined = std::make_shared<std::atomic<bool>>(false);
    auto owner = ownerJoins(scope, joined);
    CancellationToken token;
    owner.setCancellationToken(token);

    owner.resume();
    ASSERT_FALSE(owner.isDone());
    token.cancel();
    ASSERT_TRUE(owner.isDone()) << "a cancelled join must stop waiting";
    EXPECT_THROW(owner.result(), CancelledException);
    EXPECT_FALSE(joined->load());

    gate->complete();
    EXPECT_FALSE(childCancelled->load()) << "cancelling the joiner must not cancel the children";
    ASSERT_TRUE(slark::test::joinScope(scope));
}

TEST(StructuredConcurrencyTest, ScopeChildFailureCancelsSiblingsAndJoinRethrowsRootFailure) {
    AsyncScope scope;
    auto sibling = std::make_shared<CompletionAwaiter<void>>();
    auto siblingCancelled = std::make_shared<std::atomic<bool>>(false);
    scope.spawn(recordCancellation(sibling, siblingCancelled));

    auto failing = std::make_shared<CompletionAwaiter<void>>();
    scope.spawn(failAfter(failing));
    failing->complete();

    EXPECT_TRUE(siblingCancelled->load()) << "a failing child must cancel its siblings";
    auto join = slark::test::joinScopeTask(scope);
    join.resume();
    ASSERT_TRUE(waitUntilDone(join));
    try {
        join.result();
        ADD_FAILURE() << "join must rethrow the child failure";
    } catch (const CancelledException&) {
        ADD_FAILURE() << "sibling cancellation must not mask the root failure";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "boom");
    }
}
