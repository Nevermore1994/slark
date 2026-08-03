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

// W1：一个立即完成 + 一个挂起，winner 是 index 0
Task<int> parentOneWins(std::shared_ptr<CompletionAwaiter<int>> awaiter) {
    auto result = co_await when_any(simpleInt(1), pendingInt(awaiter));
    EXPECT_EQ(result.winnerIndex, 0u);
    co_return std::get<0>(result.winnerValue);
}

// W2：两个同时完成，同步驱动下 index 0 先完成
Task<int> parentBothComplete() {
    auto result = co_await when_any(simpleInt(1), simpleInt(2));
    co_return std::get<0>(result.winnerValue);
}

// W3：winner 抛异常
Task<int> parentWinnerThrows() {
    auto result = co_await when_any(throwingInt(), simpleInt(2));
    (void)result;
    co_return 0;
}

// W4：void 与非 void 混合
Task<int> parentVoidMixed() {
    auto result = co_await when_any(simpleInt(1), emptyTask());
    EXPECT_EQ(result.winnerIndex, 0u);
    co_return std::get<0>(result.winnerValue);
}

// W5：loser 挂起时 winner 完成，触发 loser 完成不得崩溃
Task<int> parentLoserFinishedLater(std::shared_ptr<CompletionAwaiter<int>> awaiter) {
    auto result = co_await when_any(simpleInt(1), pendingInt(awaiter));
    awaiter->complete(42);   // loser 之后完成：应被保活，不产生外部回调
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
        // resultTask 析构 → 内部 awaiter 析构 → childTasks_ 销毁。
        // loser (pendingInt) 仍挂起在 awaiter 上，应由 orphanTasks 保活。
    }
    // 此时触发 loser 完成：orphanTasks 中的 frame 被唤醒 → 安全到达 final suspend
    awaiter->complete(42);
    // 若 orphanTasks 已正确托管 loser frame，此处不应崩溃
}
