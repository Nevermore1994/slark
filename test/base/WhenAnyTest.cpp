// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "Awaitable.hpp"
#include "CoWhenAll.hpp"
#include "Task.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <tuple>
#include <variant>

namespace {

using slark::CompletionAwaiter;
using slark::Task;
using slark::coro::VoidResult;
using slark::coro::when_any;

Task<int> simpleInt(int value) {
    co_return value;
}

Task<int> pendingInt(std::shared_ptr<CompletionAwaiter<int>> awaiter) {
    co_return co_await *awaiter;
}

Task<void> emptyTask() {
    co_return;
}

Task<int> throwingInt() {
    throw std::runtime_error("boom");
    co_return 0;
}

Task<int> parentOneWins(std::shared_ptr<CompletionAwaiter<int>> awaiter) {
    auto result = co_await when_any(simpleInt(1), pendingInt(awaiter));
    EXPECT_EQ(result.winnerIndex, 0u);
    co_return std::get<0>(result.winnerValue);
}

Task<int> parentBothComplete() {
    auto result = co_await when_any(simpleInt(1), simpleInt(2));
    co_return std::get<0>(result.winnerValue);
}

Task<int> parentWinnerThrows() {
    auto result = co_await when_any(throwingInt(), simpleInt(2));
    (void)result;
    co_return 0;
}

Task<int> parentVoidMixed() {
    auto result = co_await when_any(simpleInt(1), emptyTask());
    EXPECT_EQ(result.winnerIndex, 0u);
    co_return std::get<0>(result.winnerValue);
}

Task<int> parentLoserFinishedLater(std::shared_ptr<CompletionAwaiter<int>> awaiter) {
    auto result = co_await when_any(simpleInt(1), pendingInt(awaiter));
    awaiter->complete(42);
    co_return std::get<0>(result.winnerValue);
}

}  // namespace

TEST(WhenAnyTest, LoserUnfinishedDoesNotBreak) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    auto task = parentOneWins(awaiter);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
}

TEST(WhenAnyTest, BothCompletedReturnsWinnerResult) {
    auto task = parentBothComplete();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
}

TEST(WhenAnyTest, WinnerThrowsIsPropagated) {
    auto task = parentWinnerThrows();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), std::runtime_error);
}

TEST(WhenAnyTest, VoidMixedReturnsWinner) {
    auto task = parentVoidMixed();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
}

TEST(WhenAnyTest, LoserCompletedLaterIsSafe) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    auto task = parentLoserFinishedLater(awaiter);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
}

TEST(WhenAnyTest, AwaiterDestroyedBeforeLoserCompletes) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    {
        auto resultTask = parentOneWins(awaiter);
        resultTask.resume();
        ASSERT_TRUE(resultTask.isDone());
    }
    awaiter->complete(42);
}
