// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "AsyncGenerator.hpp"
#include "AsyncIOOperation.hpp"
#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "CoScope.hpp"
#include "CoWhenAll.hpp"
#include "Error.h"
#include "Executor.h"
#include "CoroutineTestSupport.h"
#include "Task.hpp"
#include "ThreadTestSupport.h"
#include "Yield.hpp"

using namespace std::chrono_literals;

using slark::CancellationToken;
using slark::CancelledException;
using slark::CompletionAwaiter;
using slark::Error;
using slark::Task;
using slark::TimerAwaiter;
using slark::test::waitUntilDone;

namespace {

Task<void> waitTimer(std::chrono::milliseconds delay) {
    co_await TimerAwaiter(delay);
}

Task<void> nestedWait() {
    co_await waitTimer(5000ms);
}

Task<void> parentOfChild(Task<void> child) {
    co_await std::move(child);
}

Task<int> immediateValue() {
    co_return 42;
}

Task<void> immediateVoid() {
    co_return;
}

Task<int> awaitCompletion(std::shared_ptr<CompletionAwaiter<int>> awaiter) {
    co_return co_await *awaiter;
}

Task<void> yieldOnce() {
    co_await slark::yield();
}

class NeverCompletes final : public slark::AsyncIOOperationBase<NeverCompletes, void> {
protected:
    void startOperation() noexcept override {}
};

Task<slark::IOResult<void>> awaitNeverCompletes() {
    co_return co_await NeverCompletes{};
}

slark::AsyncGenerator<int> valueAfterTimer() {
    co_await TimerAwaiter(5000ms);
    co_yield 1;
}

Task<void> consumeFirst(slark::AsyncGenerator<int>& generator) {
    auto value = co_await generator.next();
    (void)value;
}

Task<void> whenAllWaits() {
    co_await slark::coro::when_all(waitTimer(5000ms), waitTimer(5000ms));
}

class CountingNeverCompletes final
    : public slark::AsyncIOOperationBase<CountingNeverCompletes, void> {
public:
    explicit CountingNeverCompletes(std::atomic<int>* started) : started_(started) {}

protected:
    void startOperation() noexcept override {
        started_->fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::atomic<int>* started_;
};

class CancelInsideStart final : public slark::AsyncIOOperationBase<CancelInsideStart, void> {
public:
    explicit CancelInsideStart(CancellationToken token) : token_(std::move(token)) {}

protected:
    void startOperation() noexcept override {
        token_.cancel();
    }

private:
    CancellationToken token_;
};

Task<slark::IOResult<void>> awaitCountingIo(std::atomic<int>* started) {
    co_return co_await CountingNeverCompletes(started);
}

Task<slark::IOResult<void>> awaitCancelInsideStart(CancellationToken token) {
    co_return co_await CancelInsideStart(std::move(token));
}

Task<int> whenAnyWaits(slark::coro::AsyncScope& scope) {
    auto result = co_await slark::coro::when_any(scope, waitTimer(5000ms), waitTimer(5000ms));
    co_return static_cast<int>(result.winnerIndex);
}

Task<bool> observeToken(bool* bound) {
    auto token = co_await slark::coro::cancellation_token();
    *bound = token.has_value();
    co_return token.has_value() && token->isCancelled();
}

}  // namespace

TEST(CancellationPropagationTest, TimerWakesOnCancel) {
    CancellationToken token;
    auto task = waitTimer(5000ms);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CancellationPropagationTest, CancelledTokenDoesNotSuspend) {
    CancellationToken token;
    token.cancel();
    auto task = waitTimer(5000ms);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CancellationPropagationTest, CompletionAwaiterWakesOnCancel) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    CancellationToken token;
    auto task = awaitCompletion(awaiter);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);

    awaiter->complete(7);
}

TEST(CancellationPropagationTest, YieldThrowsWhenAlreadyCancelled) {
    CancellationToken token;
    token.cancel();
    auto task = yieldOnce();
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CancellationPropagationTest, IoOperationReportsCancelledError) {
    CancellationToken token;
    auto task = awaitNeverCompletes();
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    const auto result = task.result();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Cancelled);
}

TEST(CancellationPropagationTest, DeepChainInheritsParentToken) {
    CancellationToken token;
    auto task = nestedWait();
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CancellationPropagationTest, LinkedChildTokenFollowsItsOwnSource) {
    CancellationToken parentToken;
    CancellationToken ownToken;
    auto child = waitTimer(5000ms);
    child.setCancellationToken(ownToken);
    auto task = parentOfChild(std::move(child));
    task.setCancellationToken(parentToken);
    task.resume();
    ASSERT_FALSE(task.isDone());

    ownToken.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CancellationPropagationTest, LinkedChildTokenFollowsParent) {
    CancellationToken parentToken;
    CancellationToken ownToken;
    auto child = waitTimer(5000ms);
    child.setCancellationToken(ownToken);
    auto task = parentOfChild(std::move(child));
    task.setCancellationToken(parentToken);
    task.resume();
    ASSERT_FALSE(task.isDone());

    parentToken.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CancellationPropagationTest, CancelAfterCompletionKeepsResult) {
    CancellationToken token;
    auto task = immediateValue();
    task.setCancellationToken(token);
    task.resume();
    token.cancel();
    EXPECT_EQ(task.result(), 42);
}

TEST(CancellationPropagationTest, TimerThatAlreadyFiredIgnoresLaterCancel) {
    CancellationToken token;
    auto task = waitTimer(5ms);
    task.setCancellationToken(token);
    task.resume();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_TRUE(task.isDone());
    token.cancel();
    EXPECT_NO_THROW(task.result());
}

TEST(CancellationPropagationTest, UnboundTaskCompletesNormally) {
    auto task = waitTimer(5ms);
    task.resume();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_TRUE(task.isDone());
    EXPECT_NO_THROW(task.result());
}

TEST(CancellationPropagationTest, CancellationTokenAwaiterReportsBinding) {
    bool unboundSawToken = true;
    auto unbound = observeToken(&unboundSawToken);
    unbound.resume();
    EXPECT_FALSE(unboundSawToken);
    EXPECT_FALSE(unbound.result());

    CancellationToken token;
    token.cancel();
    bool boundSawToken = false;
    auto bound = observeToken(&boundSawToken);
    bound.setCancellationToken(token);
    bound.resume();
    EXPECT_TRUE(boundSawToken);
    EXPECT_TRUE(bound.result());
}

TEST(CancellationPropagationTest, WhenAllChildrenInheritParentToken) {
    CancellationToken token;
    auto task = whenAllWaits();
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CancellationPropagationTest, GeneratorProducerInheritsConsumerToken) {
    CancellationToken token;
    auto generator = valueAfterTimer();
    auto consumer = consumeFirst(generator);
    consumer.setCancellationToken(token);
    consumer.resume();
    ASSERT_FALSE(consumer.isDone());

    token.cancel();
    ASSERT_TRUE(consumer.isDone());
    EXPECT_THROW(consumer.result(), CancelledException);
}

TEST(CancellationPropagationTest, AsyncScopeCancelAllWakesBlockedChild) {
    slark::coro::AsyncScope scope;
    scope.spawn(waitTimer(5000ms));
    const auto start = std::chrono::steady_clock::now();
    scope.cancelAll();
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

namespace {

Task<void> countAfterTimer(std::atomic<int>* ran) {
    co_await slark::TimerAwaiter(1ms);
    ran->fetch_add(1);
}

}  // namespace

TEST(CancellationPropagationTest, AsyncScopeSpawnRacingCancelAllNeverRunsRejectedChildren) {
    for (int round = 0; round < 20; ++round) {
        slark::coro::AsyncScope scope;
        std::atomic<int> accepted{0};
        std::atomic<int> ran{0};
        std::atomic<bool> go{false};
        auto spawner = [&] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            for (int i = 0; i < 50; ++i) {
                if (scope.spawn(countAfterTimer(&ran))) {
                    accepted.fetch_add(1);
                }
            }
        };
        std::thread first(spawner);
        std::thread second(spawner);
        std::thread canceller([&] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            scope.cancelAll();
        });
        go.store(true);
        first.join();
        second.join();
        canceller.join();
        ASSERT_TRUE(slark::test::joinScope(scope)) << "round " << round;
        EXPECT_LE(ran.load(), accepted.load()) << "round " << round;
    }
}

TEST(CancellationPropagationTest, AsyncScopeWaitAllDoesNotStallOnFastChildren) {
    slark::SerialExecutor executor;
    slark::coro::AsyncScope scope;
    scope.setDefaultExecutor(executor);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 200; ++i) {
        scope.spawn(immediateVoid());
        ASSERT_TRUE(slark::test::joinScope(scope));
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
}

TEST(CancellationPropagationTest, CancelAfterSuspendedTaskDestroyedIsSafe) {
    CancellationToken token;
    {
        auto task = waitTimer(5000ms);
        task.setCancellationToken(token);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }
    EXPECT_NO_THROW(token.cancel());
}

TEST(CancellationPropagationTest, CancelAfterFrameDestroyedWhileAwaiterAliveIsSafe) {
    CancellationToken token;
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    {
        auto task = awaitCompletion(awaiter);
        task.setCancellationToken(token);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }
    EXPECT_NO_THROW(token.cancel());
    EXPECT_NO_THROW(awaiter->complete(7));
}

TEST(CancellationPropagationTest, CancelAfterIoTaskDestroyedIsSafe) {
    CancellationToken token;
    {
        auto task = awaitNeverCompletes();
        task.setCancellationToken(token);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }
    EXPECT_NO_THROW(token.cancel());
}

TEST(CancellationPropagationTest, IoOperationWithCancelledTokenDoesNotStartIo) {
    CancellationToken token;
    token.cancel();
    std::atomic<int> started{0};
    auto task = awaitCountingIo(&started);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    const auto result = task.result();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Cancelled);
    EXPECT_EQ(started.load(), 0);
}

TEST(CancellationPropagationTest, CancelDuringOperationStartDoesNotSuspend) {
    CancellationToken token;
    auto task = awaitCancelInsideStart(token);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    const auto result = task.result();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::Cancelled);
}

TEST(CancellationPropagationTest, WhenAnyChildrenInheritParentToken) {
    slark::coro::AsyncScope scope;
    CancellationToken token;
    auto task = whenAnyWaits(scope);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CancellationPropagationTest, ScopeDestructionWakesBlockedChildPromptly) {
    const auto start = std::chrono::steady_clock::now();
    {
        slark::coro::AsyncScope scope;
        scope.spawn(waitTimer(5000ms));
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

TEST(CancellationPropagationTest, ScopeSpawnAfterCancelAllExitsPromptly) {
    slark::coro::AsyncScope scope;
    scope.cancelAll();
    scope.spawn(waitTimer(5000ms));
    const auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

TEST(CancellationPropagationTest, ScopeCancelAllThenJoinReleasesStuckChild) {
    slark::test::withWatchdog(
        [] {
            slark::coro::AsyncScope scope;
            scope.spawn(waitTimer(60000ms));
            scope.cancelAll();
            EXPECT_TRUE(slark::test::joinScope(scope));
        },
        15s);
}

namespace {

Task<void> suspendedJoin(slark::coro::AsyncScope* scope, std::atomic<int>* outcome) {
    try {
        co_await scope->join();
        outcome->store(1);
    } catch (const CancelledException&) {
        outcome->store(2);
    }
}

}  // namespace

TEST(CancellationPropagationTest, CancelAllWakesAJoinThatIsAlreadySuspended) {
    slark::coro::AsyncScope scope;
    scope.spawn(waitTimer(60000ms));
    std::atomic<int> outcome{0};
    auto joiner = suspendedJoin(&scope, &outcome);
    joiner.resume();
    ASSERT_FALSE(joiner.isDone());

    const auto start = std::chrono::steady_clock::now();
    std::thread canceller([&scope] {
        scope.cancelAll();
    });
    canceller.join();
    ASSERT_TRUE(waitUntilDone(joiner, 5s)) << "cancelAll must wake a suspended join";
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
    EXPECT_EQ(outcome.load(), 1) << "join returns normally once the cancelled children have finished";
}

TEST(CancellationPropagationTest, ScopeDestroyedWhileJoinIsSuspendedDoesNotDangle) {
    auto outcome = std::make_shared<std::atomic<int>>(0);
    auto joiner = std::make_shared<Task<void>>();
    {
        auto scope = std::make_unique<slark::coro::AsyncScope>();
        scope->spawn(waitTimer(60000ms));
        *joiner = suspendedJoin(scope.get(), outcome.get());
        joiner->resume();
        ASSERT_FALSE(joiner->isDone());
    }
    ASSERT_TRUE(waitUntilDone(*joiner, 5s)) << "destroying the scope must release a suspended join";
}

TEST(CancellationPropagationTest, CompleteAndCancelRaceResolvesOnce) {
    constexpr int kRounds = 300;
    int valued = 0;
    int cancelled = 0;
    for (int round = 0; round < kRounds; ++round) {
        auto awaiter = std::make_shared<CompletionAwaiter<int>>();
        CancellationToken token;
        auto task = awaitCompletion(awaiter);
        task.setCancellationToken(token);
        task.resume();
        ASSERT_FALSE(task.isDone());

        std::thread canceller([&token] { token.cancel(); });
        std::thread completer([awaiter] { awaiter->complete(7); });
        canceller.join();
        completer.join();

        ASSERT_TRUE(waitUntilDone(task));
        try {
            EXPECT_EQ(task.result(), 7);
            ++valued;
        } catch (const CancelledException&) {
            ++cancelled;
        }
    }
    EXPECT_EQ(valued + cancelled, kRounds);
}

TEST(CancellationPropagationTest, TimerFiringRacesCancelResolvesOnce) {
    constexpr int kRounds = 200;
    int finished = 0;
    int cancelled = 0;
    for (int round = 0; round < kRounds; ++round) {
        CancellationToken token;
        auto task = waitTimer(1ms);
        task.setCancellationToken(token);
        task.resume();
        std::thread canceller([&token] { token.cancel(); });
        canceller.join();

        ASSERT_TRUE(waitUntilDone(task));
        try {
            task.result();
            ++finished;
        } catch (const CancelledException&) {
            ++cancelled;
        }
    }
    EXPECT_EQ(finished + cancelled, kRounds);
}

TEST(CancellationPropagationTest, YieldRacesCancelResolvesOnce) {
    constexpr int kRounds = 200;
    int finished = 0;
    int cancelled = 0;
    for (int round = 0; round < kRounds; ++round) {
        CancellationToken token;
        auto task = yieldOnce();
        task.setCancellationToken(token);
        task.resume();
        std::thread canceller([&token] { token.cancel(); });
        canceller.join();

        ASSERT_TRUE(waitUntilDone(task));
        try {
            task.result();
            ++finished;
        } catch (const CancelledException&) {
            ++cancelled;
        }
    }
    EXPECT_EQ(finished + cancelled, kRounds);
}
