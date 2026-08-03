// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "AsyncGenerator.hpp"
#include "Awaitable.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "Task.hpp"

namespace {

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

slark::AsyncGenerator<int> twoValues() {
    co_yield 1;
    co_yield 2;
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

TEST(CompletionAwaiterTest, ChildCompletionDoesNotDeadlockFrameDestruction) {
    auto awaiter = std::make_shared<slark::CompletionAwaiter<void>>();
    auto task = parentWithChild(awaiter);

    task.resume();
    ASSERT_FALSE(task.isDone());

    awaiter->complete();

    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 42);
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
