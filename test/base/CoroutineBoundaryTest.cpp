// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "AsyncGenerator.hpp"
#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "CoScope.hpp"
#include "CoTimeout.hpp"
#include "CoWhenAll.hpp"
#include "CoroutineTestSupport.h"
#include "Error.h"
#include "Executor.h"
#include "Task.hpp"
#include "Yield.hpp"

using namespace std::chrono_literals;

namespace slark::coro {
struct AsyncScopeTestAccess {
    static std::size_t trackedChildren(const AsyncScope& scope) {
        std::lock_guard lock(scope.core_->mutex);
        return scope.core_->children.size();
    }
};
}  // namespace slark::coro

using slark::AsyncGenerator;
using slark::CancellationToken;
using slark::CancelledException;
using slark::CompletionAwaiter;
using slark::GeneratorStatus;
using slark::InlineExecutor;
using slark::SerialExecutor;
using slark::Task;
using slark::TimeoutException;
using slark::TimerAwaiter;
using slark::coro::AsyncScope;
using slark::coro::when_all;
using slark::coro::with_timeout;
using slark::test::waitUntilDone;

namespace {

Task<int> valueTask(int v) {
    co_return v;
}

Task<void> voidTask() {
    co_return;
}

Task<int> throwingTask() {
    throw std::runtime_error("boom");
    co_return 0;
}

AsyncGenerator<int> rangeGen(int n) {
    for (int i = 0; i < n; ++i) {
        co_yield i;
    }
}

AsyncGenerator<int> emptyGen() {
    co_return;
}

Task<int> sumAll(AsyncGenerator<int> gen) {
    int total = 0;
    while (auto v = co_await gen.next()) {
        total += *v;
    }
    co_return total;
}

}  // namespace


TEST(CoroutineBoundaryTest, AwaitingEmptyTaskThrowsInsteadOfCrashing) {
    auto outer = []() -> Task<int> {
        int v = co_await Task<int>{};
        co_return v;
    }();
    outer.resume();
    ASSERT_TRUE(outer.isDone());
    EXPECT_THROW(outer.result(), std::runtime_error);
}

TEST(CoroutineBoundaryTest, EmptyTaskIsNotValidAndIsDone) {
    Task<int> empty;
    EXPECT_FALSE(empty.isValid());
    EXPECT_TRUE(empty.isDone());
}

TEST(CoroutineBoundaryTest, MovedFromTaskIsInvalidAndSafeToResume) {
    auto source = valueTask(3);
    auto dest = std::move(source);
    EXPECT_FALSE(source.isValid());
    source.resume();
    EXPECT_TRUE(source.isDone());
    EXPECT_THROW(source.result(), std::runtime_error);
    dest.resume();
    EXPECT_EQ(dest.result(), 3);
}

TEST(CoroutineBoundaryTest, ResumingCompletedTaskIsIdempotent) {
    auto task = valueTask(9);
    task.resume();
    task.resume();
    task.resume();
    EXPECT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 9);
}

TEST(CoroutineBoundaryTest, ResultBeforeCompletionThrows) {
    CompletionAwaiter<int> awaiter;
    auto body = [&awaiter]() -> Task<int> {
        co_return co_await awaiter;
    };
    auto task = body();
    task.resume();
    ASSERT_FALSE(task.isDone());
    EXPECT_THROW(task.result(), std::runtime_error);
    awaiter.complete(1);
    EXPECT_TRUE(waitUntilDone(task));
}


TEST(CoroutineBoundaryTest, ExceptionThroughThreeLevelsKeepsDynamicType) {
    auto level2 = []() -> Task<int> {
        co_return co_await throwingTask();
    };
    auto level1 = [&level2]() -> Task<int> {
        co_return co_await level2();
    };
    auto topBody = [&level1]() -> Task<int> {
        co_return co_await level1();
    };
    auto top = topBody();
    top.resume();
    ASSERT_TRUE(top.isDone());
    EXPECT_THROW(top.result(), std::runtime_error);
}

TEST(CoroutineBoundaryTest, GenericHandlerInsideCoroutineObservesCancellation) {
    auto task = []() -> Task<int> {
        try {
            auto never = std::make_shared<CompletionAwaiter<int>>();
            co_return co_await *never;
        } catch (const std::exception&) {
            co_return -1;
        }
    }();
    CancellationToken token;
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());
    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), -1);
}


TEST(CoroutineBoundaryTest, EmptyGeneratorYieldsNothing) {
    auto task = sumAll(emptyGen());
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 0);
}

TEST(CoroutineBoundaryTest, TakeZeroYieldsNothingAndDoesNotPullSource) {
    auto task = sumAll(rangeGen(5).take(0));
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 0);
}

TEST(CoroutineBoundaryTest, TakeNegativeBehavesAsTakeZero) {
    auto task = sumAll(rangeGen(5).take(-3));
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 0);
}

TEST(CoroutineBoundaryTest, TakeLongerThanSourceYieldsAllValues) {
    auto task = sumAll(rangeGen(4).take(100));
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 0 + 1 + 2 + 3);
}

TEST(CoroutineBoundaryTest, NextAfterExhaustionKeepsReturningNullopt) {
    auto gen = rangeGen(1);
    auto body = [&gen]() -> Task<int> {
        int seen = 0;
        while (co_await gen.next()) {
            ++seen;
        }
        for (int i = 0; i < 3; ++i) {
            if (co_await gen.next()) {
                co_return -1;
            }
        }
        co_return seen;
    };
    auto task = body();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
}

TEST(CoroutineBoundaryTest, DropMoreThanSourceYieldsNothing) {
    auto task = sumAll(rangeGen(3).drop(10));
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 0);
}

TEST(CoroutineBoundaryTest, FilterRejectingEverythingYieldsNothing) {
    auto task = sumAll(rangeGen(5).filter([](int) { return false; }));
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 0);
}

TEST(CoroutineBoundaryTest, TakeWhileStopsAtFirstFailureAndDoesNotResume) {
    auto task = sumAll(rangeGen(10).takeWhile([](int v) { return v < 3; }));
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 0 + 1 + 2);
}

TEST(CoroutineBoundaryTest, ChainedCombinatorsComposeCorrectly) {
    auto task = sumAll(rangeGen(10)
                           .filter([](int v) { return v % 2 == 0; })
                           .transform([](int v) { return v * 10; })
                           .drop(1)
                           .take(3));
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 20 + 40 + 60);
}

TEST(CoroutineBoundaryTest, GeneratorExceptionPropagatesToConsumer) {
    auto failing = []() -> AsyncGenerator<int> {
        co_yield 1;
        throw std::runtime_error("producer failed");
    };
    auto task = sumAll(failing());
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), std::runtime_error);
}

TEST(CoroutineBoundaryTest, CloseOnFreshGeneratorIsSafe) {
    auto gen = rangeGen(3);
    EXPECT_NO_THROW(gen.close());
    EXPECT_NO_THROW(gen.close());
    EXPECT_EQ(gen.status(), GeneratorStatus::Closed);
}


TEST(CoroutineBoundaryTest, ScopeSpawnAfterCancelAllDoesNotRunBody) {
    std::atomic<int> ran{0};
    AsyncScope scope;
    scope.cancelAll();
    scope.spawn([&ran]() -> Task<void> {
        ran.fetch_add(1);
        co_return;
    }());
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_EQ(ran.load(), 1);
}

TEST(CoroutineBoundaryTest, JoinWithNoChildrenReturnsImmediately) {
    AsyncScope scope;
    const auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

TEST(CoroutineBoundaryTest, RepeatedJoinIsIdempotent) {
    AsyncScope scope;
    scope.spawn(voidTask());
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_TRUE(slark::test::joinScope(scope));
    EXPECT_TRUE(slark::test::joinScope(scope));
}

TEST(CoroutineBoundaryTest, ChildFailureCancelsScopeAndJoinRethrowsIt) {
    AsyncScope scope;
    scope.spawn(throwingTask());
    auto join = slark::test::joinScopeTask(scope);
    join.resume();
    ASSERT_TRUE(waitUntilDone(join));
    EXPECT_THROW(join.result(), std::runtime_error);
}

TEST(CoroutineBoundaryTest, ScopeWithSerialExecutorRunsChildOnWorkerThread) {
    auto executor = std::make_shared<SerialExecutor>();
    std::atomic<bool> onWorker{false};
    const auto caller = std::this_thread::get_id();
    auto body = [&onWorker, caller]() -> Task<void> {
        onWorker.store(std::this_thread::get_id() != caller);
        co_return;
    };
    AsyncScope scope;
    scope.setDefaultExecutor(executor);
    scope.spawn(body());
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_TRUE(onWorker.load());
}

TEST(CoroutineBoundaryTest, ScopeWithInlineExecutorRunsChildSynchronously) {
    auto executor = std::make_shared<InlineExecutor>();
    bool ran = false;
    AsyncScope scope;
    scope.setDefaultExecutor(executor);
    scope.spawn([&ran]() -> Task<void> {
        ran = true;
        co_return;
    }());
    EXPECT_TRUE(ran);
    ASSERT_TRUE(slark::test::joinScope(scope));
}

TEST(CoroutineBoundaryTest, StoppedExecutorRefusesChildWithoutRunningIt) {
    auto executor = std::make_shared<SerialExecutor>();
    executor->requestStop();
    std::atomic<bool> ran{false};
    AsyncScope scope;
    scope.setDefaultExecutor(executor);
    scope.spawn([&ran]() -> Task<void> {
        ran.store(true);
        co_return;
    }());
    EXPECT_FALSE(ran.load());
    EXPECT_EQ(slark::coro::AsyncScopeTestAccess::trackedChildren(scope), 0u)
        << "a refused child must not stay tracked, or the scope would wait for it forever";
}


TEST(CoroutineBoundaryTest, CompletionFirstThenAwaitReturnsValueImmediately) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    awaiter->complete(41);
    auto body = [awaiter]() -> Task<int> {
        co_return co_await *awaiter + 1;
    };
    auto task = body();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 42);
}

TEST(CoroutineBoundaryTest, DoubleCompleteKeepsFirstValue) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    auto body = [awaiter]() -> Task<int> {
        co_return co_await *awaiter;
    };
    auto task = body();
    task.resume();
    awaiter->complete(1);
    awaiter->complete(2);
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_EQ(task.result(), 1);
}

TEST(CoroutineBoundaryTest, CompleteWithExceptionRethrowsToAwaiter) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    auto body = [awaiter]() -> Task<int> {
        co_return co_await *awaiter;
    };
    auto task = body();
    task.resume();
    awaiter->fail(std::make_exception_ptr(std::runtime_error("failed")));
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_THROW(task.result(), std::runtime_error);
}

TEST(CoroutineBoundaryTest, CompleteAfterAwaiterDestroyedIsSafe) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    {
        auto body = [awaiter]() -> Task<int> {
            co_return co_await *awaiter;
        };
        auto task = body();
        task.resume();
    }
    EXPECT_NO_THROW(awaiter->complete(5));
}


TEST(CoroutineBoundaryTest, ZeroTimeoutTimesOutOrCompletes) {
    auto task = with_timeout(valueTask(4), 0ms);
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    try {
        EXPECT_EQ(task.result(), 4);
    } catch (const TimeoutException&) {
        SUCCEED();
    }
}

TEST(CoroutineBoundaryTest, VeryLargeTimeoutDoesNotExpire) {
    auto task = with_timeout(valueTask(8), std::chrono::hours(24 * 365));
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 8);
}

TEST(CoroutineBoundaryTest, NegativeTimerAwaiterDoesNotSuspend) {
    auto task = []() -> Task<int> {
        co_await TimerAwaiter(std::chrono::milliseconds(-5));
        co_return 2;
    }();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 2);
}

TEST(CoroutineBoundaryTest, ZeroTimerAwaiterDoesNotSuspend) {
    auto task = []() -> Task<int> {
        co_await TimerAwaiter(std::chrono::milliseconds(0));
        co_return 3;
    }();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 3);
}

TEST(CoroutineBoundaryTest, TimerAwaiterDestroyedBeforeFireIsSafe) {
    std::weak_ptr<int> weak;
    {
        auto payload = std::make_shared<int>(1);
        weak = payload;
        auto task = [payload]() -> Task<void> {
            co_await TimerAwaiter(50ms);
        }();
        task.resume();
    }
    std::this_thread::sleep_for(100ms);
    EXPECT_TRUE(weak.expired());
}


TEST(CoroutineBoundaryTest, WhenAllOfVoidTasksCompletes) {
    auto task = []() -> Task<void> {
        co_await when_all(voidTask(), voidTask(), voidTask());
    }();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_NO_THROW(task.result());
}

TEST(CoroutineBoundaryTest, WhenAllPropagatesFirstExceptionAfterAllFinish) {
    std::atomic<bool> sibling{false};
    auto body = [&sibling]() -> Task<void> {
        co_await when_all(throwingTask(), [&sibling]() -> Task<int> {
            sibling.store(true);
            co_return 1;
        }());
    };
    auto task = body();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), std::runtime_error);
    EXPECT_TRUE(sibling.load()) << "siblings must still run to completion";
}

namespace {

Task<void> boundaryPark(std::shared_ptr<CompletionAwaiter<void>> gate, std::atomic<bool>* passed) {
    co_await *gate;
    passed->store(true);
}

Task<void> boundaryAwaitVoid(Task<void> child) {
    co_await std::move(child);
}

Task<int> boundaryAwaitInt(Task<int> child) {
    co_return co_await std::move(child);
}

}  // namespace

TEST(CoroutineBoundaryTest, BindingsOnAnEmptyTaskAreNoOps) {
    Task<void> empty;
    CancellationToken token;
    EXPECT_NO_THROW(empty.setCancellationToken(token));
    EXPECT_NO_THROW(empty.setExecutor(std::make_shared<SerialExecutor>()));
    EXPECT_FALSE(empty.isValid());
}

TEST(CoroutineBoundaryTest, MovedFromTaskIgnoresLaterBindings) {
    Task<void> source = voidTask();
    Task<void> destination = std::move(source);
    CancellationToken token;
    EXPECT_NO_THROW(source.setCancellationToken(token));
    EXPECT_NO_THROW(source.setExecutor(std::make_shared<SerialExecutor>()));
    destination.resume();
    EXPECT_TRUE(destination.isDone());
}

TEST(CoroutineBoundaryTest, MovedStartedTaskStillRejectsBindings) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    std::atomic<bool> passed{false};
    Task<void> original = boundaryPark(gate, &passed);
    original.resume();
    Task<void> moved = std::move(original);

    CancellationToken token;
    EXPECT_THROW(moved.setCancellationToken(token), std::logic_error)
        << "the started state belongs to the frame, so it survives moving the Task";
    EXPECT_THROW(moved.setExecutor(std::make_shared<SerialExecutor>()), std::logic_error);
    gate->complete();
    ASSERT_TRUE(waitUntilDone(moved));
    EXPECT_TRUE(passed.load());
}

TEST(CoroutineBoundaryTest, CoAwaitOfAStartedSuspendedTaskIsRejectedWithoutResumingIt) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    std::atomic<bool> passed{false};
    Task<void> child = boundaryPark(gate, &passed);
    child.resume();
    ASSERT_FALSE(child.isDone());

    auto parent = boundaryAwaitVoid(std::move(child));
    parent.resume();
    ASSERT_TRUE(parent.isDone());
    EXPECT_THROW(parent.result(), std::logic_error)
        << "entering a child parked on its own wait would resume it without the event";
    EXPECT_FALSE(passed.load());

    gate->complete();
    EXPECT_FALSE(passed.load()) << "the rejected child was released and must not run on";
}

TEST(CoroutineBoundaryTest, CoAwaitOfAStartedTaskThatAlreadyCompletedReturnsItsResult) {
    Task<int> child = valueTask(7);
    child.resume();
    ASSERT_TRUE(child.isDone());

    auto parent = boundaryAwaitInt(std::move(child));
    parent.resume();
    ASSERT_TRUE(parent.isDone());
    EXPECT_EQ(parent.result(), 7);
}
