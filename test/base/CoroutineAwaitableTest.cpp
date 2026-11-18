// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "AsyncGenerator.hpp"
#include "Awaitable.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "CoroutineTestSupport.h"
#include "Executor.h"
#include "Task.hpp"

namespace {

struct UncontrolledPromise {};

template <typename Promise>
concept CanAwaitTaskFrom = requires(slark::TaskAwaiter<void>& awaiter,
                                    std::coroutine_handle<Promise> handle) {
    awaiter.await_suspend(handle);
};

static_assert(slark::ContinuationPromise<slark::TaskPromise<void>>);
static_assert(slark::ContinuationPromise<slark::detail::AsyncGeneratorPromise<int>>);
static_assert(!slark::ContinuationPromise<UncontrolledPromise>);
static_assert(!CanAwaitTaskFrom<UncontrolledPromise>);

slark::Task<int> awaitCompletion(
    std::shared_ptr<slark::CompletionAwaiter<int>> awaiter) {
    co_return co_await *awaiter;
}

slark::Task<void> awaitCompletionVoid(
    std::shared_ptr<slark::CompletionAwaiter<void>> awaiter,
    std::atomic<int>* resumed) {
    co_await *awaiter;
    resumed->fetch_add(1, std::memory_order_relaxed);
}

slark::Task<void> childCompletion(
    std::shared_ptr<slark::CompletionAwaiter<void>> awaiter) {
    co_await *awaiter;
}

slark::Task<int> parentWithChild(
    std::shared_ptr<slark::CompletionAwaiter<void>> awaiter) {
    co_await childCompletion(std::move(awaiter));
    co_return 42;
}

slark::Task<int> parentWithTwoCompletions(
    std::shared_ptr<slark::CompletionAwaiter<void>> first,
    std::shared_ptr<slark::CompletionAwaiter<void>> second) {
    co_await childCompletion(std::move(first));
    co_await *second;
    co_return 42;
}

struct TaskFrameMarker {
    std::shared_ptr<std::atomic<int>> destroyed;

    ~TaskFrameMarker() {
        destroyed->fetch_add(1, std::memory_order_relaxed);
    }
};

slark::Task<void> trackedParentWithTwoCompletions(
    std::shared_ptr<slark::CompletionAwaiter<void>> first,
    std::shared_ptr<slark::CompletionAwaiter<void>> second,
    std::shared_ptr<std::atomic<int>> destroyed) {
    TaskFrameMarker marker{std::move(destroyed)};
    co_await childCompletion(std::move(first));
    co_await *second;
}

slark::Task<int> completedTask(int value) {
    co_return value;
}

struct GeneratorFrameMarker {
    std::shared_ptr<std::atomic<int>> destroyed;

    ~GeneratorFrameMarker() {
        destroyed->fetch_add(1, std::memory_order_relaxed);
    }
};

slark::AsyncGenerator<int> twoValues() {
    co_yield 1;
    co_yield 2;
}

slark::AsyncGenerator<int> trackedValue(std::shared_ptr<std::atomic<int>> destroyed) {
    GeneratorFrameMarker marker{std::move(destroyed)};
    co_yield 1;
}

slark::Task<void> consumeAndDestroyAfterYield(std::shared_ptr<std::atomic<int>> destroyed) {
    auto generator = trackedValue(std::move(destroyed));
    auto value = co_await generator.next();
    if (!value.has_value()) {
        co_return;
    }
    generator = {};
}

slark::Task<void> consumeToFinalSuspend(std::shared_ptr<std::atomic<int>> destroyed) {
    auto generator = trackedValue(std::move(destroyed));
    auto value = co_await generator.next();
    if (!value.has_value()) {
        co_return;
    }
    co_await generator.next();
}

slark::Task<int> consumeMovedGenerator(std::shared_ptr<std::atomic<int>> destroyed) {
    auto generator = trackedValue(std::move(destroyed));
    auto value = co_await std::move(generator).next();
    co_return value.value_or(0);
}

slark::AsyncGenerator<int> awaitBeforeYield(
    std::shared_ptr<slark::CompletionAwaiter<void>> awaiter) {
    co_await *awaiter;
    co_yield 1;
}

slark::AsyncGenerator<int> trackedAwaitBeforeYield(
    std::shared_ptr<slark::CompletionAwaiter<void>> awaiter,
    std::shared_ptr<std::atomic<int>> destroyed,
    std::atomic<int>* resumed,
    std::atomic<int>* resumedAfterYield = nullptr) {
    GeneratorFrameMarker marker{std::move(destroyed)};
    co_await *awaiter;
    resumed->fetch_add(1, std::memory_order_relaxed);
    co_yield 1;
    if (resumedAfterYield) {
        resumedAfterYield->fetch_add(1, std::memory_order_relaxed);
    }
}

slark::AsyncGenerator<int> trackedThrowingGenerator(
    std::shared_ptr<std::atomic<int>> destroyed) {
    GeneratorFrameMarker marker{std::move(destroyed)};
    co_yield 1;
    throw std::runtime_error("tracked generator boom");
}

slark::Task<void> consumeOne(slark::AsyncGenerator<int>& generator,
                             std::atomic<bool>* reachedEnd) {
    auto value = co_await generator.next();
    reachedEnd->store(!value.has_value(), std::memory_order_release);
}

slark::Task<void> consumeAndCloseAfterYield(
    slark::AsyncGenerator<int>& generator,
    std::atomic<bool>* received,
    std::shared_ptr<std::atomic<int>> destroyed,
    std::atomic<bool>* destroyDeferred) {
    auto value = co_await generator.next();
    if (value.has_value()) {
        received->store(true, std::memory_order_release);
        generator.close();
        destroyDeferred->store(
            destroyed->load(std::memory_order_relaxed) == 0, std::memory_order_release);
    }
}

slark::Task<void> consumeTemporaryGenerator(
    std::shared_ptr<slark::CompletionAwaiter<void>> awaiter,
    std::shared_ptr<std::atomic<int>> destroyed,
    std::atomic<int>* resumed,
    std::atomic<bool>* reachedEnd) {
    auto value = co_await trackedAwaitBeforeYield(
                         std::move(awaiter), std::move(destroyed), resumed)
                         .next();
    reachedEnd->store(!value.has_value(), std::memory_order_release);
}

slark::Task<void> drainGenerator(slark::AsyncGenerator<int>& generator) {
    while (true) {
        auto value = co_await generator.next();
        if (!value.has_value()) {
            co_return;
        }
    }
}

slark::Task<int> consumeTwoValues() {
    auto generator = twoValues();
    auto first = co_await generator.next();
    auto second = co_await generator.next();
    auto end = co_await generator.next();
    co_return first.value() + second.value() + (end.has_value() ? 100 : 0);
}

slark::AsyncGenerator<int> throwingGenerator() {
    co_yield 1;
    throw std::runtime_error("generator boom");
    co_yield 2;
}

slark::Task<int> consumeThrowingGenerator() {
    auto generator = throwingGenerator();
    auto first = co_await generator.next();
    auto second = co_await generator.next();
    co_return first.value() + (second.has_value() ? 10 : 20);
}

}  // namespace

TEST(CompletionAwaiterTest, CompletionBeforeAwaitSuspensionIsConsumed) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<int>>();
    awaiter->complete(42);

    auto task = awaitCompletion(awaiter);
    task.resume();

    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 42);
}

TEST(CompletionAwaiterTest, CompletionAfterSuspensionResumesExactlyOnce) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    std::atomic<int> resumed{0};
    auto task = awaitCompletionVoid(awaiter, &resumed);

    task.resume();
    ASSERT_FALSE(task.isDone());

    awaiter->complete();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(resumed.load(std::memory_order_relaxed), 1);

    awaiter->complete();
    EXPECT_EQ(resumed.load(std::memory_order_relaxed), 1);
}

TEST(CompletionAwaiterTest, ConcurrentCompletionHasSingleWinner) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<int>>();
    auto task = awaitCompletion(awaiter);
    task.resume();
    ASSERT_FALSE(task.isDone());

    std::vector<std::thread> workers;
    for (int i = 0; i < 16; ++i) {
        workers.emplace_back([awaiter, i] { awaiter->complete(i); });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    ASSERT_TRUE(task.isDone());
    auto value = task.result();
    EXPECT_GE(value, 0);
    EXPECT_LT(value, 16);
}

TEST(CompletionAwaiterTest, PendingCompletionCannotResumeDestroyedFrame) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<int>>();
    {
        auto task = awaitCompletion(awaiter);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }

    EXPECT_NO_THROW(awaiter->complete(7));
}

TEST(CompletionAwaiterTest, FrameOfAWaiterOrphanedByCompleterIsReleasedWithItsOwner) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<int>>();
    auto owner = std::make_shared<slark::Task<int>>(awaitCompletion(awaiter));
    std::weak_ptr<slark::Task<int>> weakOwner = owner;
    owner->resume();
    ASSERT_FALSE(owner->isDone());

    awaiter.reset();
    EXPECT_FALSE(owner->isDone()) << "an orphaned waiter stays suspended, as documented";
    owner.reset();
    EXPECT_TRUE(weakOwner.expired()) << "the orphaned waiter's frame must be released with its owner";
}

TEST(CompletionAwaiterTest, ChildCompletionDoesNotDeadlockFrameDestruction) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    auto task = parentWithChild(awaiter);

    task.resume();
    ASSERT_FALSE(task.isDone());

    awaiter->complete();

    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 42);
}

TEST(CompletionAwaiterTest, PendingChildCannotResumeDestroyedParent) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    {
        auto task = parentWithChild(awaiter);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }

    EXPECT_NO_THROW(awaiter->complete());
}

TEST(TaskLeaseTest, MovedLeaseProtectsFrameAfterOwnerDestruction) {
    slark::ExecutionLease lease;

    {
        auto task = completedTask(42);
        auto handle = task.getHandle();
        auto movedLease = handle.promise().frameControl()->tryAcquire(handle);
        ASSERT_TRUE(movedLease);
        lease = std::move(movedLease);
        EXPECT_FALSE(movedLease);

        task.resume();
    }

    EXPECT_TRUE(lease);
    lease.reset();
}

TEST(GeneratorStateTest, CloseTransitionsToClosed) {
    auto generator = twoValues();

    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Running);
    generator.close();
    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Closed);
    EXPECT_TRUE(generator.isDone());
}

TEST(GeneratorStateTest, CloseResumesPendingConsumerAsEndOfStream) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    auto generator = awaitBeforeYield(awaiter);
    std::atomic<bool> reachedEnd{false};
    auto consumer = consumeOne(generator, &reachedEnd);

    consumer.resume();
    ASSERT_FALSE(consumer.isDone());
    ASSERT_EQ(generator.status(), slark::GeneratorStatus::Running);

    generator.close();

    EXPECT_TRUE(consumer.isDone());
    EXPECT_TRUE(reachedEnd.load(std::memory_order_acquire));
    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Closed);

    awaiter->complete();
}

TEST(GeneratorStateTest, OwnerDestructorClosesPendingConsumer) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    std::atomic<bool> reachedEnd{false};
    std::optional<slark::Task<void>> consumer;

    {
        auto generator = awaitBeforeYield(awaiter);
        consumer.emplace(consumeOne(generator, &reachedEnd));
        consumer->resume();
        ASSERT_FALSE(consumer->isDone());
    }

    ASSERT_TRUE(consumer->isDone());
    EXPECT_TRUE(reachedEnd.load(std::memory_order_acquire));
    awaiter->complete();
}

TEST(GeneratorCloseTest, CloseDuringExternalWaitPreventsLaterGateResume) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    std::atomic<int> resumed{0};
    std::atomic<bool> reachedEnd{false};
    auto generator = trackedAwaitBeforeYield(awaiter, destroyed, &resumed);
    auto consumer = consumeOne(generator, &reachedEnd);

    consumer.resume();
    ASSERT_FALSE(consumer.isDone());
    EXPECT_EQ(generator.phase(), slark::GeneratorPhase::WaitingExternal);
    ASSERT_TRUE(generator.getHandle());

    generator.close();
    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Closed);
    EXPECT_EQ(generator.phase(), slark::GeneratorPhase::FinalSuspended);
    EXPECT_FALSE(generator.getHandle());
    EXPECT_TRUE(consumer.isDone());
    EXPECT_TRUE(reachedEnd.load(std::memory_order_acquire));
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);

    awaiter->complete();
    EXPECT_EQ(resumed.load(std::memory_order_relaxed), 0);
    generator.close();
    awaiter->complete();
}

TEST(GeneratorCloseTest, CloseAndCompletionRaceHasAtMostOneProducerResume) {
    constexpr int kIterations = 200;
    for (int i = 0; i < kIterations; ++i) {
        auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
        auto destroyed = std::make_shared<std::atomic<int>>(0);
        std::atomic<int> resumed{0};
        std::atomic<bool> reachedEnd{false};
        auto generator = trackedAwaitBeforeYield(awaiter, destroyed, &resumed);
        auto consumer = consumeOne(generator, &reachedEnd);

        consumer.resume();
        ASSERT_FALSE(consumer.isDone());
        std::atomic<bool> start{false};
        std::thread completer([awaiter, &start] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            awaiter->complete();
        });
        start.store(true, std::memory_order_release);
        generator.close();
        completer.join();

        EXPECT_TRUE(consumer.isDone());
        EXPECT_EQ(generator.status(), slark::GeneratorStatus::Closed);
        EXPECT_FALSE(generator.getHandle());
        EXPECT_LE(resumed.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
        awaiter->complete();
    }
}

TEST(GeneratorCloseTest, CloseDuringYieldHandoffDefersDestroyUntilLeaseRelease) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    std::atomic<int> resumed{0};
    std::atomic<int> resumedAfterYield{0};
    std::atomic<bool> received{false};
    std::atomic<bool> destroyDeferred{false};
    auto generator = trackedAwaitBeforeYield(
        awaiter, destroyed, &resumed, &resumedAfterYield);
    auto consumer = consumeAndCloseAfterYield(
        generator, &received, destroyed, &destroyDeferred);

    consumer.resume();
    ASSERT_FALSE(consumer.isDone());
    awaiter->complete();

    ASSERT_TRUE(consumer.isDone());
    EXPECT_TRUE(received.load(std::memory_order_acquire));
    EXPECT_TRUE(destroyDeferred.load(std::memory_order_acquire));
    EXPECT_EQ(resumed.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(resumedAfterYield.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Closed);
    EXPECT_FALSE(generator.getHandle());
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
}

TEST(GeneratorCloseTest, CloseAfterCompletedDestroysFrameWithoutChangingStatus) {
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    auto generator = trackedValue(destroyed);
    auto consumer = drainGenerator(generator);

    consumer.resume();
    ASSERT_TRUE(consumer.isDone());
    ASSERT_EQ(generator.status(), slark::GeneratorStatus::Completed);
    ASSERT_TRUE(generator.getHandle());

    generator.close();
    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Completed);
    EXPECT_FALSE(generator.getHandle());
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
    generator.close();
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
}

TEST(GeneratorCloseTest, CloseAfterFailedDestroysFrameWithoutChangingStatus) {
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    auto generator = trackedThrowingGenerator(destroyed);
    auto consumer = drainGenerator(generator);

    consumer.resume();
    ASSERT_TRUE(consumer.isDone());
    ASSERT_EQ(generator.status(), slark::GeneratorStatus::Failed);
    ASSERT_TRUE(generator.getHandle());
    EXPECT_THROW(consumer.result(), std::runtime_error);

    generator.close();
    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Failed);
    EXPECT_FALSE(generator.getHandle());
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
    generator.close();
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
}

TEST(GeneratorCloseTest, DoubleCloseIsIdempotentAndDoesNotResumeConsumerTwice) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    std::atomic<int> resumed{0};
    std::atomic<bool> reachedEnd{false};
    auto generator = trackedAwaitBeforeYield(awaiter, destroyed, &resumed);
    auto consumer = consumeOne(generator, &reachedEnd);

    consumer.resume();
    ASSERT_FALSE(consumer.isDone());
    generator.close();
    generator.close();

    ASSERT_TRUE(consumer.isDone());
    EXPECT_TRUE(reachedEnd.load(std::memory_order_acquire));
    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Closed);
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
    awaiter->complete();
    EXPECT_EQ(resumed.load(std::memory_order_relaxed), 0);
}

TEST(GeneratorCloseTest, DestroyingRvalueNextAwaiterClosesOwnedGenerator) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    std::atomic<int> resumed{0};
    std::atomic<bool> reachedEnd{false};

    {
        auto consumer = consumeTemporaryGenerator(awaiter, destroyed, &resumed, &reachedEnd);
        consumer.resume();
        ASSERT_FALSE(consumer.isDone());
        consumer = slark::Task<void>{};
    }

    EXPECT_FALSE(reachedEnd.load(std::memory_order_acquire));
    EXPECT_EQ(resumed.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
    awaiter->complete();
    EXPECT_EQ(resumed.load(std::memory_order_relaxed), 0);
}

TEST(GeneratorStateTest, NormalReturnTransitionsToCompleted) {
    auto generator = twoValues();
    auto consumer = drainGenerator(generator);

    consumer.resume();

    ASSERT_TRUE(consumer.isDone());
    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Completed);
}

TEST(GeneratorStateTest, UnhandledExceptionTransitionsToFailed) {
    auto generator = throwingGenerator();
    auto consumer = drainGenerator(generator);

    consumer.resume();

    ASSERT_TRUE(consumer.isDone());
    EXPECT_EQ(generator.status(), slark::GeneratorStatus::Failed);
    EXPECT_THROW(consumer.result(), std::runtime_error);
}

TEST(CompletionAwaiterTest, ContinuationLeaseSurvivesUntilNextSuspension) {
    auto first = std::make_shared<slark::CompletionAwaiter<void>>();
    auto second = std::make_shared<slark::CompletionAwaiter<void>>();
    auto task = parentWithTwoCompletions(first, second);

    task.resume();
    ASSERT_FALSE(task.isDone());

    first->complete();
    ASSERT_FALSE(task.isDone());

    second->complete();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 42);
}

TEST(CompletionAwaiterTest, TransferLeaseIsReleasedBeforePendingCompletion) {
    auto first = std::make_shared<slark::CompletionAwaiter<void>>();
    auto second = std::make_shared<slark::CompletionAwaiter<void>>();
    auto destroyed = std::make_shared<std::atomic<int>>(0);

    {
        auto task = trackedParentWithTwoCompletions(first, second, destroyed);
        task.resume();
        ASSERT_FALSE(task.isDone());

        first->complete();
        ASSERT_FALSE(task.isDone());
    }

    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
    EXPECT_NO_THROW(second->complete());
}

TEST(CompletionAwaiterTest, MultipleWaitersAreAllWokenWithTheValue) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<int>>();
    auto first = awaitCompletion(awaiter);
    auto second = awaitCompletion(awaiter);
    first.resume();
    second.resume();
    ASSERT_FALSE(first.isDone());
    ASSERT_FALSE(second.isDone());

    awaiter->complete(7);
    ASSERT_TRUE(slark::test::waitUntilDone(first));
    ASSERT_TRUE(slark::test::waitUntilDone(second));
    EXPECT_EQ(first.result(), 7);
    EXPECT_EQ(second.result(), 7);
}

TEST(CompletionAwaiterTest, MultipleWaitersAreAllWokenWithTheException) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<int>>();
    auto first = awaitCompletion(awaiter);
    auto second = awaitCompletion(awaiter);
    first.resume();
    second.resume();
    ASSERT_FALSE(first.isDone());
    ASSERT_FALSE(second.isDone());

    awaiter->fail(std::make_exception_ptr(std::runtime_error("failed")));
    ASSERT_TRUE(slark::test::waitUntilDone(first));
    ASSERT_TRUE(slark::test::waitUntilDone(second));
    EXPECT_THROW(first.result(), std::runtime_error);
    EXPECT_THROW(second.result(), std::runtime_error);
}

TEST(CompletionAwaiterTest, CancellingOneWaiterLeavesTheOthersSuspended) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<int>>();
    slark::CancellationToken token;
    auto cancelled = awaitCompletion(awaiter);
    cancelled.setCancellationToken(token);
    auto other = awaitCompletion(awaiter);
    cancelled.resume();
    other.resume();
    ASSERT_FALSE(cancelled.isDone());
    ASSERT_FALSE(other.isDone());

    token.cancel();
    ASSERT_TRUE(slark::test::waitUntilDone(cancelled));
    EXPECT_THROW(cancelled.result(), slark::CancelledException);
    EXPECT_FALSE(other.isDone());

    awaiter->complete(9);
    ASSERT_TRUE(slark::test::waitUntilDone(other));
    EXPECT_EQ(other.result(), 9);
}

TEST(CompletionAwaiterTest, DestroyedWaiterIsUnlinkedAndTheOthersStillWake) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<int>>();
    auto survivor = awaitCompletion(awaiter);
    survivor.resume();
    {
        auto gone = awaitCompletion(awaiter);
        gone.resume();
        ASSERT_FALSE(gone.isDone());
    }
    ASSERT_FALSE(survivor.isDone());

    awaiter->complete(4);
    ASSERT_TRUE(slark::test::waitUntilDone(survivor));
    EXPECT_EQ(survivor.result(), 4);
}

TEST(CompletionAwaiterTest, CompleteRacingRegistrationWakesEveryWaiter) {
    constexpr int kRounds = 200;
    constexpr int kWaiters = 4;
    for (int round = 0; round < kRounds; ++round) {
        auto awaiter = std::make_shared<slark::CompletionAwaiter<int>>();
        std::vector<slark::Task<int>> tasks;
        for (int i = 0; i < kWaiters; ++i) {
            tasks.push_back(awaitCompletion(awaiter));
        }
        std::thread completer([awaiter] { awaiter->complete(5); });
        for (auto& task : tasks) {
            task.resume();
        }
        completer.join();
        for (auto& task : tasks) {
            ASSERT_TRUE(slark::test::waitUntilDone(task));
            EXPECT_EQ(task.result(), 5);
        }
    }
}

namespace {

template <typename U>
concept NamesCompletionAwaiter = requires { typename slark::CompletionAwaiter<U>; };

}  // namespace

static_assert(!NamesCompletionAwaiter<std::unique_ptr<int>>);
static_assert(NamesCompletionAwaiter<std::shared_ptr<int>>);

TEST(CompletionAwaiterTest, ValueTypeMustBeCopyableSoEveryWaiterGetsTheValue) {
    SUCCEED();
}

namespace {

slark::AsyncGenerator<int> yieldAfterGate(std::shared_ptr<slark::CompletionAwaiter<void>> gate) {
    co_await *gate;
    co_yield 1;
}

slark::Task<std::optional<int>> nextFrom(slark::AsyncGenerator<int>& generator) {
    co_return co_await generator.next();
}

}  // namespace

TEST(AsyncGeneratorTest, SetExecutorAfterFirstNextThrows) {
    auto gate = std::make_shared<slark::CompletionAwaiter<void>>();
    auto generator = yieldAfterGate(gate);
    auto consumer = nextFrom(generator);
    consumer.resume();
    ASSERT_FALSE(consumer.isDone());

    auto executor = std::make_shared<slark::SerialExecutor>();
    EXPECT_THROW(generator.setExecutor(executor), std::logic_error)
        << "the producer's domain cannot change once its frame has started";

    gate->complete();
    ASSERT_TRUE(slark::test::waitUntilDone(consumer));
    auto value = consumer.result();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 1);
}

namespace {

slark::AsyncGenerator<int> singleValueGenerator() {
    co_yield 1;
}

slark::AsyncGenerator<int> recordThreadAfterGate(
    std::shared_ptr<slark::CompletionAwaiter<void>> gate,
    std::atomic<std::thread::id>* resumedOn) {
    co_await *gate;
    resumedOn->store(std::this_thread::get_id());
    co_yield 1;
}

std::thread::id threadOnExecutor(slark::IExecutor& executor) {
    std::promise<std::thread::id> probe;
    auto id = probe.get_future();
    executor.execute([p = std::move(probe)]() mutable {
        p.set_value(std::this_thread::get_id());
    });
    return id.get();
}

}  // namespace

TEST(AsyncGeneratorTest, SetExecutorBeforeFirstNextRunsTheProducerOnItsDomain) {
    auto executor = std::make_shared<slark::SerialExecutor>();
    const auto worker = threadOnExecutor(*executor);
    auto gate = std::make_shared<slark::CompletionAwaiter<void>>();
    std::atomic<std::thread::id> resumedOn{};
    auto generator = recordThreadAfterGate(gate, &resumedOn);
    generator.setExecutor(executor);
    auto consumer = nextFrom(generator);
    consumer.resume();
    ASSERT_FALSE(consumer.isDone());

    gate->complete();
    ASSERT_TRUE(slark::test::waitUntilDone(consumer));
    EXPECT_EQ(resumedOn.load(), worker) << "the producer's wait must resume on the bound domain";
    auto value = consumer.result();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 1);
}

TEST(AsyncGeneratorTest, SetExecutorNullBeforeFirstNextClearsTheDomain) {
    auto executor = std::make_shared<slark::SerialExecutor>();
    auto gate = std::make_shared<slark::CompletionAwaiter<void>>();
    std::atomic<std::thread::id> resumedOn{};
    auto generator = recordThreadAfterGate(gate, &resumedOn);
    generator.setExecutor(executor);
    generator.setExecutor(nullptr);
    auto consumer = nextFrom(generator);
    consumer.resume();
    ASSERT_FALSE(consumer.isDone());

    const auto completer = std::this_thread::get_id();
    gate->complete();
    ASSERT_TRUE(slark::test::waitUntilDone(consumer));
    EXPECT_EQ(resumedOn.load(), completer) << "a cleared domain resumes on the notifying thread";
}

TEST(AsyncGeneratorTest, SetExecutorNullAfterFirstNextThrows) {
    auto gate = std::make_shared<slark::CompletionAwaiter<void>>();
    auto generator = yieldAfterGate(gate);
    auto consumer = nextFrom(generator);
    consumer.resume();
    ASSERT_FALSE(consumer.isDone());
    EXPECT_THROW(generator.setExecutor(nullptr), std::logic_error)
        << "clearing the domain of a started producer is rejected like any other binding";
    gate->complete();
    ASSERT_TRUE(slark::test::waitUntilDone(consumer));
}

TEST(AsyncGeneratorTest, SetExecutorAfterCloseIsANoOp) {
    auto gate = std::make_shared<slark::CompletionAwaiter<void>>();
    auto generator = yieldAfterGate(gate);
    auto consumer = nextFrom(generator);
    consumer.resume();
    ASSERT_FALSE(consumer.isDone());

    generator.close();
    ASSERT_TRUE(slark::test::waitUntilDone(consumer));
    EXPECT_NO_THROW(generator.setExecutor(std::make_shared<slark::SerialExecutor>()))
        << "a closed generator has no producer left to bind";
    gate->complete();
    auto value = consumer.result();
    EXPECT_FALSE(value.has_value()) << "close ends the stream for the pending consumer";
}

TEST(AsyncGeneratorTest, SetExecutorAfterCompletionThrows) {
    auto generator = singleValueGenerator();
    auto first = nextFrom(generator);
    first.resume();
    ASSERT_TRUE(first.isDone());
    auto firstValue = first.result();
    ASSERT_TRUE(firstValue.has_value());
    EXPECT_EQ(*firstValue, 1);

    auto second = nextFrom(generator);
    second.resume();
    ASSERT_TRUE(second.isDone());
    auto end = second.result();
    EXPECT_FALSE(end.has_value());
    EXPECT_THROW(generator.setExecutor(std::make_shared<slark::SerialExecutor>()),
                 std::logic_error)
        << "a completed producer has started, so its domain is fixed";
}

TEST(AsyncGeneratorTest, SetExecutorOnAMovedFromOrEmptyGeneratorIsANoOp) {
    auto generator = singleValueGenerator();
    auto owner = std::move(generator);
    EXPECT_NO_THROW(generator.setExecutor(std::make_shared<slark::SerialExecutor>()));

    slark::AsyncGenerator<int> empty;
    EXPECT_NO_THROW(empty.setExecutor(std::make_shared<slark::SerialExecutor>()));
}

TEST(AsyncGeneratorTest, ProducerTokenIsLinkedWithTheConsumerTokenOnFirstNext) {
    for (int source = 0; source < 2; ++source) {
        auto gate = std::make_shared<slark::CompletionAwaiter<void>>();
        auto generator = yieldAfterGate(gate);
        slark::CancellationToken producerToken;
        slark::CancellationToken consumerToken;
        generator.setCancellationToken(producerToken);
        auto consumer = nextFrom(generator);
        consumer.setCancellationToken(consumerToken);
        consumer.resume();
        ASSERT_FALSE(consumer.isDone());

        (source == 0 ? consumerToken : producerToken).cancel();
        ASSERT_TRUE(slark::test::waitUntilDone(consumer)) << "source " << source;
        EXPECT_THROW(consumer.result(), slark::CancelledException)
            << "either token must reach the producer's wait, source " << source;
        gate->complete();
    }
}

TEST(AsyncGeneratorTest, SetCancellationTokenAfterFirstNextThrows) {
    auto gate = std::make_shared<slark::CompletionAwaiter<void>>();
    auto generator = yieldAfterGate(gate);
    auto consumer = nextFrom(generator);
    consumer.resume();
    ASSERT_FALSE(consumer.isDone());

    slark::CancellationToken token;
    EXPECT_THROW(generator.setCancellationToken(token), std::logic_error)
        << "the producer's token cannot change once its frame has started";
    gate->complete();
    ASSERT_TRUE(slark::test::waitUntilDone(consumer));
    EXPECT_TRUE(consumer.result().has_value());
}

TEST(AsyncGeneratorTest, SecondPendingNextIsRejectedWithoutDrivingTheProducer) {
    auto gate = std::make_shared<slark::CompletionAwaiter<void>>();
    auto generator = yieldAfterGate(gate);
    auto first = nextFrom(generator);
    first.resume();
    ASSERT_FALSE(first.isDone());
    ASSERT_EQ(generator.phase(), slark::GeneratorPhase::WaitingExternal);

    auto second = nextFrom(generator);
    second.resume();
    ASSERT_TRUE(second.isDone()) << "a rejected next() must end at once";
    EXPECT_THROW(second.result(), std::logic_error);
    EXPECT_FALSE(first.isDone()) << "the producer must stay parked on its own wait";
    EXPECT_EQ(generator.phase(), slark::GeneratorPhase::WaitingExternal);

    gate->complete();
    ASSERT_TRUE(slark::test::waitUntilDone(first));
    auto value = first.result();
    ASSERT_TRUE(value.has_value()) << "the rejected call must not take over the pending consumer";
    EXPECT_EQ(*value, 1);
}

TEST(AsyncGeneratorTest, NextIsRejectedWhileTheProducerServesADestroyedConsumer) {
    auto gate = std::make_shared<slark::CompletionAwaiter<void>>();
    auto generator = yieldAfterGate(gate);
    {
        auto abandoned = nextFrom(generator);
        abandoned.resume();
        ASSERT_FALSE(abandoned.isDone());
    }

    auto late = nextFrom(generator);
    late.resume();
    ASSERT_TRUE(late.isDone());
    EXPECT_THROW(late.result(), std::logic_error)
        << "the producer is still serving the abandoned request";

    gate->complete();
    auto later = nextFrom(generator);
    later.resume();
    ASSERT_TRUE(later.isDone()) << "the abandoned request's value is kept for the next caller";
    auto value = later.result();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 1);
}

TEST(AsyncGeneratorTest, ConsumerIsResumedAfterYield) {
    auto task = consumeTwoValues();

    task.resume();

    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 3);
}

TEST(AsyncGeneratorTest, ProducerExceptionIsRethrownToConsumer) {
    auto task = consumeThrowingGenerator();

    task.resume();

    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), std::runtime_error);
}

TEST(AsyncGeneratorTest, GeneratorCanBeDestroyedAfterYieldHandoff) {
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    auto task = consumeAndDestroyAfterYield(destroyed);

    task.resume();

    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
}

TEST(AsyncGeneratorTest, GeneratorCanBeDestroyedAfterFinalHandoff) {
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    auto task = consumeToFinalSuspend(destroyed);

    task.resume();

    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
}

TEST(AsyncGeneratorTest, RvalueNextOwnsAndDestroysGeneratorFrame) {
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    auto task = consumeMovedGenerator(destroyed);

    task.resume();

    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
    EXPECT_EQ(destroyed->load(std::memory_order_relaxed), 1);
}

TEST(AsyncGeneratorTest, SynchronousHandoffRemainsStableUnderRepetition) {
    constexpr int kIterations = 1000;
    for (int i = 0; i < kIterations; ++i) {
        auto destroyed = std::make_shared<std::atomic<int>>(0);
        auto task = consumeToFinalSuspend(destroyed);
        task.resume();
        ASSERT_TRUE(task.isDone());
        ASSERT_EQ(destroyed->load(std::memory_order_relaxed), 1);
    }
}

TEST(CompletionAwaiterTest, ResetRepeatsCompletionCycles) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    for (int i = 0; i < 20; ++i) {
        std::atomic<int> resumed{0};
        auto task = awaitCompletionVoid(awaiter, &resumed);
        task.resume();
        ASSERT_FALSE(task.isDone());

        awaiter->complete();
        ASSERT_TRUE(task.isDone());
        EXPECT_EQ(resumed.load(std::memory_order_relaxed), 1);

        awaiter->reset();
        EXPECT_FALSE(awaiter->isCompleted());
    }
}

TEST(CompletionAwaiterTest, ResetBeforeAwaitRearmsSuspension) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    awaiter->complete();
    ASSERT_TRUE(awaiter->isCompleted());

    awaiter->reset();
    EXPECT_FALSE(awaiter->isCompleted());

    std::atomic<int> resumed{0};
    auto task = awaitCompletionVoid(awaiter, &resumed);
    task.resume();
    EXPECT_FALSE(task.isDone());

    awaiter->complete();
    EXPECT_TRUE(task.isDone());
    EXPECT_EQ(resumed.load(std::memory_order_relaxed), 1);
}

TEST(CompletionAwaiterTest, ResetSurvivesConcurrentCompletionCycles) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    for (int i = 0; i < 20; ++i) {
        std::atomic<int> resumed{0};
        auto task = awaitCompletionVoid(awaiter, &resumed);
        task.resume();
        ASSERT_FALSE(task.isDone());

        std::thread completer([awaiter] {
            awaiter->complete();
        });
        completer.join();

        ASSERT_TRUE(task.isDone());
        EXPECT_EQ(resumed.load(std::memory_order_relaxed), 1);
        awaiter->reset();
    }
}
