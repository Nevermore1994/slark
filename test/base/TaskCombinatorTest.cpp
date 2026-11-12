// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <exception>
#include <stdexcept>
#include <string>

#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "Error.h"
#include "Task.hpp"

using namespace std::chrono_literals;

using slark::CancellationToken;
using slark::CancelledException;
using slark::Task;
using slark::TimerAwaiter;

namespace {

Task<int> valueTask(int value) {
    co_return value;
}

Task<void> voidTask() {
    co_return;
}

Task<int> throwingValue() {
    throw std::runtime_error("boom");
    co_return 0;
}

Task<void> throwingVoid() {
    throw std::runtime_error("void boom");
    co_return;
}

Task<void> waitTimer(std::chrono::milliseconds delay) {
    co_await TimerAwaiter(delay);
}

}  // namespace

TEST(TaskCombinatorTest, ThenMapsValue) {
    auto task = valueTask(20).then([](int v) { return v * 2 + 2; });
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 42);
}

TEST(TaskCombinatorTest, ThenRunsAfterVoidSource) {
    auto task = voidTask().then([] { return 5; });
    task.resume();
    EXPECT_EQ(task.result(), 5);
}

TEST(TaskCombinatorTest, ThenSkipsFunctionOnException) {
    bool called = false;
    auto task = throwingValue().then([&called](int v) {
        called = true;
        return v;
    });
    task.resume();
    EXPECT_THROW(task.result(), std::runtime_error);
    EXPECT_FALSE(called);
}

TEST(TaskCombinatorTest, AndThenChainsTask) {
    auto task = valueTask(3).and_then([](int v) { return valueTask(v + 1); });
    task.resume();
    EXPECT_EQ(task.result(), 4);
}

TEST(TaskCombinatorTest, AndThenSkipsOnException) {
    bool called = false;
    auto task = throwingValue().and_then([&called](int v) {
        called = true;
        return valueTask(v);
    });
    task.resume();
    EXPECT_THROW(task.result(), std::runtime_error);
    EXPECT_FALSE(called);
}

TEST(TaskCombinatorTest, OrElseRecoversFromException) {
    auto task = throwingValue().or_else([](std::exception_ptr) { return 9; });
    task.resume();
    EXPECT_EQ(task.result(), 9);
}

TEST(TaskCombinatorTest, OrElseReceivesTheException) {
    std::string message;
    auto task = throwingValue().or_else([&message](std::exception_ptr error) {
        try {
            std::rethrow_exception(error);
        } catch (const std::runtime_error& e) {
            message = e.what();
        }
        return 1;
    });
    task.resume();
    EXPECT_EQ(task.result(), 1);
    EXPECT_EQ(message, "boom");
}

TEST(TaskCombinatorTest, OrElseVoidRecoversFromException) {
    bool handled = false;
    auto task = throwingVoid().or_else([&handled](std::exception_ptr) { handled = true; });
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_NO_THROW(task.result());
    EXPECT_TRUE(handled);
}

TEST(TaskCombinatorTest, OrElseDoesNotSwallowCancellation) {
    CancellationToken token;
    token.cancel();
    bool handled = false;
    auto task = waitTimer(5000ms).or_else([&handled](std::exception_ptr) { handled = true; });
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
    EXPECT_FALSE(handled);
}

TEST(TaskCombinatorTest, ThenVoidSourceVoidResultRunsFunction) {
    bool ran = false;
    auto task = voidTask().then([&ran] { ran = true; });
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_NO_THROW(task.result());
    EXPECT_TRUE(ran);
}

TEST(TaskCombinatorTest, ThenValueSourceVoidResultPassesValue) {
    int seen = 0;
    auto task = valueTask(6).then([&seen](int v) { seen = v; });
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_NO_THROW(task.result());
    EXPECT_EQ(seen, 6);
}

TEST(TaskCombinatorTest, AndThenVoidSourceChainsTask) {
    auto task = voidTask().and_then([] { return valueTask(4); });
    task.resume();
    EXPECT_EQ(task.result(), 4);
}

TEST(TaskCombinatorTest, OrElseKeepsValueWhenNoException) {
    bool handled = false;
    auto task = valueTask(5).or_else([&handled](std::exception_ptr) {
        handled = true;
        return -1;
    });
    task.resume();
    EXPECT_EQ(task.result(), 5);
    EXPECT_FALSE(handled);
}

TEST(TaskCombinatorTest, OrElseVoidKeepsCompletionWhenNoException) {
    bool handled = false;
    auto task = voidTask().or_else([&handled](std::exception_ptr) { handled = true; });
    task.resume();
    EXPECT_NO_THROW(task.result());
    EXPECT_FALSE(handled);
}

TEST(TaskCombinatorTest, ThenPropagatesCancellationAndSkipsFunction) {
    CancellationToken token;
    bool called = false;
    auto task = waitTimer(5000ms).then([&called] {
        called = true;
        return 1;
    });
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), CancelledException);
    EXPECT_FALSE(called);
}

TEST(TaskCombinatorTest, CombinatorsChainAcrossTaskTypes) {
    auto task = valueTask(1)
                    .then([](int v) { return v + 1; })
                    .and_then([](int v) { return valueTask(v * 10); })
                    .or_else([](std::exception_ptr) { return -1; });
    task.resume();
    EXPECT_EQ(task.result(), 20);
}

TEST(TaskCombinatorTest, EmptyTaskReportsInvalidOrRecovers) {
    auto thenTask = Task<int>{}.then([](int v) { return v; });
    thenTask.resume();
    EXPECT_THROW(thenTask.result(), std::runtime_error);

    auto andThenTask = Task<int>{}.and_then([](int v) { return valueTask(v); });
    andThenTask.resume();
    EXPECT_THROW(andThenTask.result(), std::runtime_error);

    auto orElseTask = Task<int>{}.or_else([](std::exception_ptr) { return 9; });
    orElseTask.resume();
    EXPECT_EQ(orElseTask.result(), 9);
}
