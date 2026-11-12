// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <thread>

#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "CoTimeout.hpp"
#include "CoroutineTestSupport.h"
#include "Error.h"
#include "Task.hpp"

using namespace std::chrono_literals;

using slark::CancellationToken;
using slark::CancelledException;
using slark::Task;
using slark::TimeoutException;
using slark::TimerAwaiter;
using slark::coro::with_timeout;
using slark::test::waitUntilDone;

namespace {

Task<int> immediateValue(int value) {
    co_return value;
}

Task<int> slowValue(std::chrono::milliseconds delay) {
    co_await TimerAwaiter(delay);
    co_return 1;
}

Task<void> slowVoid(std::chrono::milliseconds delay) {
    co_await TimerAwaiter(delay);
}

Task<void> recordExit(std::atomic<bool>* exited, std::chrono::milliseconds delay) {
    try {
        co_await TimerAwaiter(delay);
    } catch (const CancelledException&) {
        exited->store(true);
        throw;
    }
}

Task<int> failingValue() {
    throw std::logic_error("child failed");
    co_return 0;
}

}  // namespace

TEST(CoTimeoutTest, CompletedChildReturnsValue) {
    auto task = with_timeout(immediateValue(7), 1000ms);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 7);
}

TEST(CoTimeoutTest, ChildExceptionPassesThrough) {
    auto task = with_timeout(failingValue(), 1000ms);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), std::logic_error);
}

TEST(CoTimeoutTest, ExpiryThrowsTimeoutAfterChildExits) {
    std::atomic<bool> exited{false};
    auto task = with_timeout(recordExit(&exited, 5000ms), 30ms);
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_THROW(task.result(), TimeoutException);
    EXPECT_TRUE(exited.load());
}

TEST(CoTimeoutTest, ExpiryCancelsVoidChild) {
    auto task = with_timeout(slowVoid(5000ms), 30ms);
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_THROW(task.result(), TimeoutException);
}

TEST(CoTimeoutTest, ParentCancellationIsNotReportedAsTimeout) {
    CancellationToken token;
    auto task = with_timeout(slowValue(5000ms), 5000ms);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CoTimeoutTest, OrElseRecoversFromExpiry) {
    auto task = with_timeout(slowValue(5000ms), 30ms).or_else([](std::exception_ptr) {
        return -1;
    });
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_EQ(task.result(), -1);
}

TEST(CoTimeoutTest, InnerTimeoutPropagatesWithoutOuterExpiry) {
    auto task = with_timeout(with_timeout(slowValue(5000ms), 30ms), 5000ms);
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_THROW(task.result(), TimeoutException);
}

TEST(CoTimeoutTest, OuterTimeoutCancelsNestedChildAndReportsTimeout) {
    std::atomic<bool> exited{false};
    auto task = with_timeout(with_timeout(recordExit(&exited, 5000ms), 5000ms), 30ms);
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_THROW(task.result(), TimeoutException);
    EXPECT_TRUE(exited.load());
}

TEST(CoTimeoutTest, AlreadyCancelledParentReportsCancellation) {
    CancellationToken token;
    token.cancel();
    auto task = with_timeout(slowValue(5000ms), 30ms);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CoTimeoutTest, ExpiryRacingCompletionResolvesOnce) {
    constexpr int kRounds = 100;
    int values = 0;
    int timeouts = 0;
    for (int round = 0; round < kRounds; ++round) {
        auto task = with_timeout(slowValue(2ms), 2ms);
        task.resume();
        ASSERT_TRUE(waitUntilDone(task));
        try {
            EXPECT_EQ(task.result(), 1);
            ++values;
        } catch (const TimeoutException&) {
            ++timeouts;
        }
    }
    EXPECT_EQ(values + timeouts, kRounds);
}

TEST(CoTimeoutTest, EmptyChildReportsInvalidTask) {
    auto task = with_timeout(Task<int>{}, 1000ms);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), std::runtime_error);
}

namespace {

Task<int> swallowingChild() {
    try {
        co_await TimerAwaiter(5000ms);
    } catch (const CancelledException&) {
        co_return 7;
    }
    co_return 0;
}

Task<int> erroringChild() {
    try {
        co_await TimerAwaiter(5000ms);
    } catch (const CancelledException&) {
        throw std::runtime_error("late failure");
    }
    co_return 0;
}

}  // namespace

TEST(CoTimeoutTest, SwallowedCancelIsReportedAsTimeout) {
    auto task = with_timeout(swallowingChild(), 30ms);
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_THROW(task.result(), TimeoutException);
}

TEST(CoTimeoutTest, ErrorAfterExpiryIsReplacedByTimeout) {
    auto task = with_timeout(erroringChild(), 30ms);
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_THROW(task.result(), TimeoutException);
}

TEST(CoTimeoutTest, ParentCancelIsReportedEvenIfChildSwallowsIt) {
    CancellationToken token;
    auto task = with_timeout(swallowingChild(), 5000ms);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_THROW(task.result(), CancelledException);
}

TEST(CoTimeoutTest, ZeroTimeoutExpiresBeforeTheChildCompletes) {
    auto task = with_timeout(slowValue(5000ms), 0ms);
    task.resume();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_THROW(task.result(), TimeoutException);
}

TEST(CoTimeoutTest, ParentCancelRacingCompletionResolvesOnce) {
    constexpr int kRounds = 100;
    int values = 0;
    int cancelled = 0;
    for (int round = 0; round < kRounds; ++round) {
        CancellationToken token;
        auto task = with_timeout(slowValue(2ms), 5000ms);
        task.setCancellationToken(token);
        task.resume();
        std::thread canceller([&token] { token.cancel(); });
        canceller.join();

        ASSERT_TRUE(waitUntilDone(task));
        try {
            EXPECT_EQ(task.result(), 1);
            ++values;
        } catch (const CancelledException&) {
            ++cancelled;
        }
    }
    EXPECT_EQ(values + cancelled, kRounds);
}
