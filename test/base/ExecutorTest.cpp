//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

#include "Awaitable.hpp"
#include "CoroutineTestSupport.h"
#include "Executor.h"
#include "Task.hpp"

using slark::CompletionAwaiter;
using slark::SerialExecutor;
using slark::Task;
using slark::test::waitUntilDone;

namespace {

using ThreadSlot = std::atomic<std::thread::id>;

std::thread::id threadOf(slark::IExecutor& executor) {
    std::promise<std::thread::id> probe;
    auto id = probe.get_future();
    executor.execute([p = std::move(probe)]() mutable {
        p.set_value(std::this_thread::get_id());
    });
    return id.get();
}

Task<void> recordAfterWait(std::shared_ptr<CompletionAwaiter<void>> gate, ThreadSlot* slot) {
    co_await *gate;
    slot->store(std::this_thread::get_id());
}

Task<void> middleLayer(std::shared_ptr<CompletionAwaiter<void>> gate, ThreadSlot* slot) {
    co_await recordAfterWait(std::move(gate), slot);
}

Task<void> rootLayer(std::shared_ptr<CompletionAwaiter<void>> gate, ThreadSlot* slot) {
    co_await middleLayer(std::move(gate), slot);
}

Task<void> awaitGiven(Task<void> child) {
    co_await std::move(child);
}

}  // namespace

TEST(ExecutorTest, ChildWithoutExecutorResumesOnTheRootExecutorThroughTwoLevels) {
    auto executor = std::make_shared<SerialExecutor>();
    const auto worker = threadOf(*executor);
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    ThreadSlot resumedOn{};
    auto root = rootLayer(gate, &resumedOn);
    root.setExecutor(executor);
    root.resume();
    ASSERT_FALSE(root.isDone());

    gate->complete();
    ASSERT_TRUE(waitUntilDone(root));
    EXPECT_EQ(resumedOn.load(), worker)
        << "a child with no domain of its own must inherit the domain of its awaiting parent";
}

TEST(ExecutorTest, ChildKeepsItsOwnExecutorOverTheAwaitingParentsDomain) {
    auto parentDomain = std::make_shared<SerialExecutor>();
    auto childDomain = std::make_shared<SerialExecutor>();
    const auto parentWorker = threadOf(*parentDomain);
    const auto childWorker = threadOf(*childDomain);
    ASSERT_NE(parentWorker, childWorker);

    auto gate = std::make_shared<CompletionAwaiter<void>>();
    ThreadSlot resumedOn{};
    auto child = recordAfterWait(gate, &resumedOn);
    child.setExecutor(childDomain);
    auto root = awaitGiven(std::move(child));
    root.setExecutor(parentDomain);
    root.resume();
    ASSERT_FALSE(root.isDone());

    gate->complete();
    ASSERT_TRUE(waitUntilDone(root));
    EXPECT_EQ(resumedOn.load(), childWorker) << "an explicit child domain must win";
    EXPECT_NE(resumedOn.load(), parentWorker);
}

TEST(ExecutorTest, SetExecutorAfterFirstResumeThrows) {
    auto executor = std::make_shared<SerialExecutor>();
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    ThreadSlot resumedOn{};
    auto root = recordAfterWait(gate, &resumedOn);
    root.resume();
    ASSERT_FALSE(root.isDone());

    EXPECT_THROW(root.setExecutor(executor), std::logic_error)
        << "the wait already registered its gate on the previous domain";
    gate->complete();
    ASSERT_TRUE(waitUntilDone(root));
}

namespace {

Task<void> finishImmediately() {
    co_return;
}

}  // namespace

TEST(ExecutorTest, SetExecutorAfterCompletionThrows) {
    auto task = finishImmediately();
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_THROW(task.setExecutor(std::make_shared<SerialExecutor>()), std::logic_error);
}

TEST(ExecutorTest, SetExecutorNullBeforeStartClearsTheDomain) {
    auto executor = std::make_shared<SerialExecutor>();
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    ThreadSlot resumedOn{};
    auto root = recordAfterWait(gate, &resumedOn);
    root.setExecutor(executor);
    root.setExecutor(nullptr);
    root.resume();
    ASSERT_FALSE(root.isDone());

    const auto completer = std::this_thread::get_id();
    gate->complete();
    ASSERT_TRUE(waitUntilDone(root));
    EXPECT_EQ(resumedOn.load(), completer) << "a cleared domain resumes on the notifying thread";
}

TEST(ExecutorTest, SetExecutorNullAfterFirstResumeThrows) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    ThreadSlot resumedOn{};
    auto root = recordAfterWait(gate, &resumedOn);
    root.resume();
    ASSERT_FALSE(root.isDone());
    EXPECT_THROW(root.setExecutor(nullptr), std::logic_error)
        << "clearing the domain of a started task is rejected like any other binding";
    gate->complete();
    ASSERT_TRUE(waitUntilDone(root));
}
