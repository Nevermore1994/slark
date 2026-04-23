//
// Created by Nevermore on 2026/4/2.
// Slark Coroutine Test
// Copyright (c) 2026 Nevermore All rights reserved.
//
#include "Coroutine.hpp"

#include "CancellationToken.h"
#include "CoroutineScheduler.hpp"
#include "ThreadPool.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace slark;

// Coroutine Basic Tests
TEST(CoroutineTest, CreateAndResume) {
    int value = 0;
    auto coro = [&value]() -> Coroutine<void> {
        value = 42;
        co_return;
    }();

    EXPECT_EQ(value, 0);
    coro.resume();
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(coro.isDone());
}

TEST(CoroutineTest, ReturnValue) {
    auto coro = []() -> Coroutine<int> {
        co_return 123;
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), 123);
}

TEST(CoroutineTest, StringReturn) {
    auto coro = []() -> Coroutine<std::string> {
        co_return "hello";
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), "hello");
}

TEST(CoroutineTest, MoveOnlyReturn) {
    auto coro = []() -> Coroutine<std::unique_ptr<int>> {
        co_return std::make_unique<int>(999);
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(*coro.getResult(), 999);
}

TEST(CoroutineTest, ExceptionHandling) {
    auto coro = []() -> Coroutine<int> {
        throw std::runtime_error("test exception");
        co_return 0;
    }();

    EXPECT_NO_THROW(coro.resume());
    EXPECT_TRUE(coro.isDone());
}

TEST(CoroutineTest, IsValid) {
    auto coro = []() -> Coroutine<void> {
        co_return;
    }();
    EXPECT_TRUE(coro.isValid());

    Coroutine<void> moved = std::move(coro);
    EXPECT_FALSE(coro.isValid());
    EXPECT_TRUE(moved.isValid());
}

TEST(CoroutineTest, Cancel) {
    auto coro = []() -> Coroutine<int> {
        co_return 42;
    }();

    coro.cancel();
    EXPECT_TRUE(coro.isCancelled());
}

TEST(CoroutineTest, MoveSemantics) {
    auto coro1 = []() -> Coroutine<int> {
        co_return 10;
    }();

    auto coro2 = []() -> Coroutine<int> {
        co_return 20;
    }();

    coro1.resume();
    coro2.resume();

    EXPECT_EQ(coro1.getResult(), 10);
    EXPECT_EQ(coro2.getResult(), 20);
}

// CancellationToken Tests
TEST(CancellationTokenTest, DefaultNotCancelled) {
    CancellationToken token;
    EXPECT_FALSE(token.isCancelled());
    EXPECT_TRUE(token); // operator bool()
}

TEST(CancellationTokenTest, Cancel) {
    CancellationToken token;
    token.cancel();
    EXPECT_TRUE(token.isCancelled());
    EXPECT_FALSE(token); // operator bool()
}

TEST(CancellationTokenTest, CopySharesState) {
    CancellationToken token1;
    CancellationToken token2 = token1;

    token1.cancel();
    EXPECT_TRUE(token2.isCancelled());
}

TEST(CancellationTokenTest, MultipleCancellations) {
    CancellationToken token;
    token.cancel();
    token.cancel(); // Should be idempotent
    EXPECT_TRUE(token.isCancelled());
}

// CoroutineScheduler Tests
TEST(CoroutineSchedulerTest, SingletonInstance) {
    auto& scheduler1 = CoroutineScheduler::instance();
    auto& scheduler2 = CoroutineScheduler::instance();
    EXPECT_EQ(&scheduler1, &scheduler2);
}

TEST(CoroutineSchedulerTest, ScheduleImmediate) {
    auto& scheduler = CoroutineScheduler::instance();

    std::atomic<int> counter{0};
    auto coro = [&]() -> Coroutine<void> {
        counter++;
        co_return;
    }();

    scheduler.schedule(coro.getHandle());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(counter.load(), 1);
}

TEST(CoroutineSchedulerTest, ScheduleAfter) {
    auto& scheduler = CoroutineScheduler::instance();

    std::atomic<int> counter{0};
    auto coro = [&]() -> Coroutine<void> {
        counter++;
        co_return;
    }();

    // 显式转换为 std::coroutine_handle<> 避免歧义
    std::coroutine_handle<> handle = coro.getHandle();
    auto timerId = scheduler.scheduleAfter(std::chrono::milliseconds(50), handle);
    EXPECT_TRUE(timerId.isValid());

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(counter.load(), 1);
}

TEST(CoroutineSchedulerTest, CancelTimerByTimerId) {
    auto& scheduler = CoroutineScheduler::instance();

    std::atomic<int> counter{0};
    auto coro = [&]() -> Coroutine<void> {
        counter++;
        co_return;
    }();

    std::coroutine_handle<> handle = coro.getHandle();
    auto timerId = scheduler.scheduleAfter(std::chrono::milliseconds(200), handle);
    scheduler.cancel(timerId);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(counter.load(), 0);
}

TEST(CoroutineSchedulerTest, ScheduleCoroutineTemplate) {
    auto& scheduler = CoroutineScheduler::instance();

    std::atomic<int> counter{0};
    auto coro = [&]() -> Coroutine<int> {
        counter++;
        co_return 42;
    }();

    // 显式转换为 std::coroutine_handle<> 避免歧义
    std::coroutine_handle<> handle = coro.getHandle();
    auto timerId = scheduler.scheduleAfter(std::chrono::milliseconds(30), handle);
    EXPECT_TRUE(timerId.isValid());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter.load(), 1);
}

// ThreadPool Basic Tests (avoid name collision with ThreadPoolTest.cpp which uses TEST_F)
TEST(ThreadPoolBasicTest, PostJob) {
    ThreadPool pool(ThreadPoolConfig{2});

    std::atomic<int> counter{0};
    auto result = pool.postJob([&]() {
        counter++;
    });
    EXPECT_TRUE(result.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter.load(), 1);
}

TEST(ThreadPoolBasicTest, MultipleJobs) {
    ThreadPool pool(ThreadPoolConfig{4});

    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i) {
        pool.postJob([&]() {
            counter++;
        });
    }

    pool.waitForAll();
    EXPECT_EQ(counter.load(), 100);
}

TEST(ThreadPoolBasicTest, SubmitWithFuture) {
    ThreadPool pool(ThreadPoolConfig{2});

    auto futureResult = pool.submit([]() -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 42;
    });

    EXPECT_TRUE(futureResult.has_value());
    auto value = futureResult->get();
    EXPECT_EQ(value, 42);
}

TEST(ThreadPoolBasicTest, Clear) {
    ThreadPool pool(ThreadPoolConfig{2});

    for (int i = 0; i < 100; ++i) {
        pool.postJob([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    }

    pool.clear();
    EXPECT_EQ(pool.getJobCount(), 0);
}

TEST(ThreadPoolBasicTest, JobPriority) {
    ThreadPool pool(ThreadPoolConfig{1});

    std::vector<int> order;
    std::mutex orderMutex;

    auto record = [&](int value) {
        std::lock_guard lock(orderMutex);
        order.push_back(value);
    };

    // Post jobs with different priorities
    pool.postJob(JobPriority::Low, [&]() {
        record(3);
    });
    pool.postJob(JobPriority::High, [&]() {
        record(1);
    });
    pool.postJob(JobPriority::Normal, [&]() {
        record(2);
    });

    pool.waitForAll();

    // High priority should execute first
    EXPECT_EQ(order.size(), 3u);
}

TEST(ThreadPoolBasicTest, GetThreadCount) {
    ThreadPoolConfig config;
    config.threadCount = 8;

    ThreadPool pool(config);
    EXPECT_EQ(pool.getThreadCount(), 8u);
}

TEST(ThreadPoolBasicTest, IsShutdown) {
    ThreadPool pool(ThreadPoolConfig{2});
    EXPECT_FALSE(pool.isShutdown());
}

// Integration Tests
TEST(CoroutineIntegrationTest, AsyncOperation) {
    ThreadPool pool(ThreadPoolConfig{2});

    auto coro = [&]() -> Coroutine<int> {
        co_return 42;
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), 42);
}

TEST(CoroutineIntegrationTest, ChainedCoroutines) {
    auto coro1 = []() -> Coroutine<int> {
        co_return 10;
    }();

    coro1.resume();
    int value = coro1.getResult();

    auto coro2 = [value]() -> Coroutine<int> {
        co_return value * 2;
    }();

    coro2.resume();
    EXPECT_EQ(coro2.getResult(), 20);
}

TEST(CoroutineIntegrationTest, CancellationTokenInCoroutine) {
    CancellationToken token;
    std::atomic<bool> completed{false};

    auto coro = [&](CancellationToken& t) -> Coroutine<void> {
        if (t.isCancelled()) {
            co_return;
        }
        completed = true;
        co_return;
    }(token);

    coro.resume();
    EXPECT_TRUE(completed.load());

    // Now test with cancelled token
    completed = false;
    CancellationToken cancelledToken;
    cancelledToken.cancel();

    auto coro2 = [&](CancellationToken& t) -> Coroutine<void> {
        if (t.isCancelled()) {
            co_return;
        }
        completed = true;
        co_return;
    }(cancelledToken);

    coro2.resume();
    EXPECT_FALSE(completed.load());
}

TEST(CoroutineIntegrationTest, CoroutineWithThreadPool) {
    ThreadPool pool(ThreadPoolConfig{2});
    std::atomic<int> result{0};

    pool.postJob([&]() {
        auto coro = []() -> Coroutine<int> {
            co_return 99;
        }();

        coro.resume();
        result = coro.getResult();
    });

    pool.waitForAll();
    EXPECT_EQ(result.load(), 99);
}

// TimerId Tests
TEST(TimerIdTest, InvalidTimerId) {
    TimerId invalidId;
    EXPECT_FALSE(invalidId.isValid());
    EXPECT_EQ(invalidId.id(), 0u);
}

TEST(TimerIdTest, NextGeneratesUniqueIds) {
    auto id1 = TimerId::next();
    auto id2 = TimerId::next();
    EXPECT_NE(id1, id2);
}

TEST(TimerIdTest, Comparison) {
    auto id1 = TimerId::next();
    auto id2 = TimerId::next();

    EXPECT_TRUE(id1 != id2);
    EXPECT_TRUE(id1 == id1);
}