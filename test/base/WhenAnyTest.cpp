// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "Awaitable.hpp"
#include "CoScope.hpp"
#include "CoWhenAll.hpp"
#include "CoroutineTestSupport.h"
#include "Task.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>

namespace {

using slark::CancelledException;
using slark::CompletionAwaiter;
using slark::Task;
using slark::coro::AsyncScope;
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

Task<void> recordingLoser(std::shared_ptr<CompletionAwaiter<int>> awaiter, std::string* outcome) {
    try {
        *outcome = "value=" + std::to_string(co_await *awaiter);
    } catch (const CancelledException&) {
        *outcome = "cancelled";
    }
}

Task<int> parentOneWins(AsyncScope& scope, std::shared_ptr<CompletionAwaiter<int>> awaiter) {
    auto result = co_await when_any(scope, simpleInt(1), pendingInt(awaiter));
    EXPECT_EQ(result.winnerIndex, 0u);
    co_return std::get<0>(result.winnerValue);
}

Task<int> parentBothComplete(AsyncScope& scope) {
    auto result = co_await when_any(scope, simpleInt(1), simpleInt(2));
    co_return std::get<0>(result.winnerValue);
}

Task<int> parentWinnerThrows(AsyncScope& scope) {
    auto result = co_await when_any(scope, throwingInt(), simpleInt(2));
    (void)result;
    co_return 0;
}

Task<int> parentVoidMixed(AsyncScope& scope) {
    auto result = co_await when_any(scope, simpleInt(1), emptyTask());
    EXPECT_EQ(result.winnerIndex, 0u);
    co_return std::get<0>(result.winnerValue);
}

Task<int> parentLoserFinishedLater(AsyncScope& scope,
                                   std::shared_ptr<CompletionAwaiter<int>> awaiter) {
    auto result = co_await when_any(scope, simpleInt(1), pendingInt(awaiter));
    awaiter->complete(42);
    co_return std::get<0>(result.winnerValue);
}

Task<int> parentRecordingLoser(AsyncScope& scope, std::shared_ptr<CompletionAwaiter<int>> awaiter,
                               std::string* outcome) {
    auto result = co_await when_any(scope, simpleInt(1), recordingLoser(awaiter, outcome));
    co_return static_cast<int>(result.winnerIndex);
}

}  // namespace

TEST(WhenAnyTest, LoserUnfinishedDoesNotBreak) {
    AsyncScope scope;
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    auto task = parentOneWins(scope, awaiter);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
}

TEST(WhenAnyTest, BothCompletedReturnsWinnerResult) {
    AsyncScope scope;
    auto task = parentBothComplete(scope);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
}

TEST(WhenAnyTest, WinnerThrowsIsPropagated) {
    AsyncScope scope;
    auto task = parentWinnerThrows(scope);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), std::runtime_error);
}

TEST(WhenAnyTest, VoidMixedReturnsWinner) {
    AsyncScope scope;
    auto task = parentVoidMixed(scope);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
}

TEST(WhenAnyTest, LoserCompletedLaterIsSafe) {
    AsyncScope scope;
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    auto task = parentLoserFinishedLater(scope, awaiter);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 1);
}

TEST(WhenAnyTest, AwaiterDestroyedBeforeLoserCompletes) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    {
        AsyncScope scope;
        auto resultTask = parentOneWins(scope, awaiter);
        resultTask.resume();
        ASSERT_TRUE(resultTask.isDone());
    }
    awaiter->complete(42);
}

TEST(WhenAnyTest, LoserIsNotCancelledWhenWinnerFinishes) {
    AsyncScope scope;
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    std::string outcome;
    auto task = parentRecordingLoser(scope, awaiter, &outcome);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 0);

    awaiter->complete(42);
    EXPECT_EQ(outcome, "value=42");
}

TEST(WhenAnyTest, ScopeCancelAllCancelsLoser) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    std::string outcome;
    {
        AsyncScope scope;
        auto task = parentRecordingLoser(scope, awaiter, &outcome);
        task.resume();
        ASSERT_TRUE(task.isDone());
        scope.cancelAll();
    }
    EXPECT_EQ(outcome, "cancelled");
}

namespace {

Task<int> failingAfter(std::shared_ptr<CompletionAwaiter<int>> gate, std::string message) {
    (void)co_await *gate;
    throw std::runtime_error(message);
    co_return 0;
}

Task<int> parentWithFailingLoser(AsyncScope& scope,
                                 std::shared_ptr<CompletionAwaiter<int>> pending,
                                 std::shared_ptr<CompletionAwaiter<int>> failing,
                                 std::string* outcome) {
    auto result = co_await when_any(scope, simpleInt(1), recordingLoser(pending, outcome),
                                    failingAfter(failing, "loser failed"));
    co_return static_cast<int>(result.winnerIndex);
}

Task<int> parentPendingLoserThenThrowingWinner(AsyncScope& scope,
                                               std::shared_ptr<CompletionAwaiter<int>> pending,
                                               std::string* outcome) {
    auto result = co_await when_any(scope, recordingLoser(pending, outcome), throwingInt());
    co_return static_cast<int>(result.winnerIndex);
}

}  // namespace

TEST(WhenAnyTest, LoserFailureCancelsScopeAndJoinRethrowsIt) {
    AsyncScope scope;
    auto pending = std::make_shared<CompletionAwaiter<int>>();
    auto failing = std::make_shared<CompletionAwaiter<int>>();
    std::string outcome;
    auto parent = parentWithFailingLoser(scope, pending, failing, &outcome);
    parent.resume();
    ASSERT_TRUE(parent.isDone());
    EXPECT_EQ(parent.result(), 0);

    failing->complete(1);
    EXPECT_EQ(outcome, "cancelled") << "a failing loser must cancel the siblings still running";

    auto join = slark::test::joinScopeTask(scope);
    join.resume();
    ASSERT_TRUE(slark::test::waitUntilDone(join));
    EXPECT_THROW(join.result(), std::runtime_error);
}

TEST(WhenAnyTest, WinnerFailureCancelsPendingLosersAndJoinRethrowsIt) {
    AsyncScope scope;
    auto pending = std::make_shared<CompletionAwaiter<int>>();
    std::string outcome;
    auto parent = parentPendingLoserThenThrowingWinner(scope, pending, &outcome);
    parent.resume();
    ASSERT_TRUE(parent.isDone());
    EXPECT_THROW(parent.result(), std::runtime_error);
    EXPECT_EQ(outcome, "cancelled") << "a failing winner fails the scope too, as in Kotlin";

    auto join = slark::test::joinScopeTask(scope);
    join.resume();
    ASSERT_TRUE(slark::test::waitUntilDone(join));
    EXPECT_THROW(join.result(), std::runtime_error);
}

TEST(WhenAnyTest, CancelledLosersDoNotFailTheScope) {
    AsyncScope scope;
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    std::string outcome;
    auto parent = parentRecordingLoser(scope, awaiter, &outcome);
    parent.resume();
    ASSERT_TRUE(parent.isDone());
    scope.cancelAll();
    EXPECT_EQ(outcome, "cancelled");

    auto join = slark::test::joinScopeTask(scope);
    join.resume();
    ASSERT_TRUE(slark::test::waitUntilDone(join));
    EXPECT_NO_THROW(join.result());
}

namespace {

Task<void> parentWhenAnyRecording(AsyncScope& scope,
                                  std::shared_ptr<slark::test::ThreadsAroundTimer> first,
                                  std::shared_ptr<slark::test::ThreadsAroundTimer> second) {
    (void)co_await when_any(scope, slark::test::recordThreadsAroundTimer(std::move(first)),
                            slark::test::recordThreadsAroundTimer(std::move(second)));
}

}  // namespace

TEST(WhenAnyTest, ChildrenStayOnTheScopeDomainAcrossASuspension) {
    auto executor = std::make_shared<slark::SerialExecutor>();
    const auto worker = slark::test::workerThreadOf(*executor);
    auto first = std::make_shared<slark::test::ThreadsAroundTimer>();
    auto second = std::make_shared<slark::test::ThreadsAroundTimer>();
    AsyncScope scope(executor);
    auto parent = parentWhenAnyRecording(scope, first, second);
    parent.resume();
    ASSERT_TRUE(slark::test::waitUntilDone(parent));
    ASSERT_TRUE(slark::test::joinScope(scope)) << "the loser keeps running until it finishes";
    EXPECT_EQ(first->before, worker);
    EXPECT_EQ(first->after, worker) << "a resume after a suspension must stay on the scope's domain";
    EXPECT_EQ(second->before, worker);
    EXPECT_EQ(second->after, worker);
}

