// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "CoScope.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

#include "Awaitable.hpp"
#include "CoWhenAll.hpp"
#include "CoroutineTestSupport.h"
#include "Executor.h"
#include "Task.hpp"

using namespace slark;
using namespace slark::coro;

namespace slark::coro {
struct AsyncScopeTestAccess {
    static std::size_t trackedChildren(const AsyncScope& scope) {
        std::lock_guard lock(scope.core_->mutex);
        return scope.core_->children.size();
    }
};
}  // namespace slark::coro

Task<int> simpleInt(int v) {
    co_return v;
}

Task<void> emptyTask() {
    co_return;
}

template <typename Func>
Task<void> runVoidTask(Func func) {
    co_await func();
}

template <typename Func>
Task<int> runIntTask(Func func) {
    co_return co_await func();
}

TEST(CoScopeTest, SpawnAndWait) {
    AsyncScope scope;
    int result = 0;

    scope.spawn(runVoidTask([&result]() -> Task<void> {
        int v = co_await simpleInt(42);
        result = v;
    }));

    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_EQ(result, 42);
}

TEST(CoScopeTest, MultipleSpawns) {
    AsyncScope scope;
    std::atomic<int> counter{0};

    for (int i = 0; i < 10; ++i) {
        scope.spawn(runVoidTask([&counter, i]() -> Task<void> {
            auto v = co_await simpleInt(i);
            counter.fetch_add(v);
        }));
    }

    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_EQ(counter.load(), 45);
}

TEST(CoScopeTest, CancelAll) {
    AsyncScope scope;
    std::atomic<bool> ran{false};

    scope.spawn(runVoidTask([&ran]() -> Task<void> {
        ran = true;
        co_return;
    }));

    scope.cancelAll();
    ASSERT_TRUE(slark::test::joinScope(scope));
}

TEST(CoScopeTest, CancelBeforeJoin) {
    AsyncScope scope;

    scope.cancelAll();
    ASSERT_TRUE(slark::test::joinScope(scope));
}

TEST(CoScopeTest, DestroyWithBlockedChildIsSafe) {
    auto blocker = std::make_shared<CompletionAwaiter<void>>();
    {
        AsyncScope scope;
        scope.spawn(runVoidTask([&blocker]() -> Task<void> {
            co_await *blocker;
        }));
    }
    blocker->complete();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

TEST(CoWhenAllTest, TwoTasks) {
    auto task = []() -> Task<int> {
        auto [a, b] = co_await when_all(simpleInt(10), simpleInt(20));
        co_return a + b;
    }();

    task.resume();
    EXPECT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 30);
}

TEST(CoWhenAllTest, VoidTasks) {
    bool a{false}, b{false};

    auto task = runVoidTask([&]() -> Task<void> {
        co_await when_all(
            runVoidTask([&a]() -> Task<void> {
                a = true;
                co_return;
            }),
            runVoidTask([&b]() -> Task<void> {
                b = true;
                co_return;
            }));
        co_return;
    });

    task.resume();
    EXPECT_TRUE(a);
    EXPECT_TRUE(b);
}

TEST(CoWhenAllTest, MixedTasksSeparate) {
    int intResult = 0;
    bool voidResult = false;

    auto task = runVoidTask([&]() -> Task<void> {
        co_await when_all(
            runVoidTask([&intResult]() -> Task<void> {
                intResult = co_await simpleInt(42);
            }),
            runVoidTask([&voidResult]() -> Task<void> {
                voidResult = true;
                co_return;
            }));
        co_return;
    });

    task.resume();
    EXPECT_EQ(intResult, 42);
    EXPECT_TRUE(voidResult);
}

TEST(CoWhenAllTest, ThreeTasks) {
    auto task = []() -> Task<int> {
        auto [a, b, c] = co_await when_all(simpleInt(1), simpleInt(2), simpleInt(3));
        co_return a + b + c;
    }();

    task.resume();
    EXPECT_EQ(task.result(), 6);
}

TEST(CoWhenAllTest, ExceptionInTask) {
    auto task = []() -> Task<int> {
        int sum = 0;
        EXPECT_THROW(co_await when_all(simpleInt(10),
                                       []() -> Task<int> {
                                           throw std::runtime_error("child fail");
                                           co_return 0;
                                       }()),
                     std::runtime_error);
        co_return sum;
    }();

    task.resume();
}

TEST(CoWhenAllTest, WhenAllWithExecutor) {
    auto exec = std::make_shared<InlineExecutor>();
    auto task = runIntTask([&]() -> Task<int> {
        auto [a, b] = co_await when_all(*exec, simpleInt(5), simpleInt(15));
        co_return a + b;
    });

    task.resume();
    EXPECT_EQ(task.result(), 20);
}

TEST(CoWhenAllTest, CoWhenAllInScope) {
    AsyncScope scope;
    int result = 0;

    scope.spawn(runVoidTask([&result]() -> Task<void> {
        auto [a, b] = co_await when_all(simpleInt(7), simpleInt(8));
        result = a + b;
    }));

    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_EQ(result, 15);
}

TEST(TaskTest, TaskWithInlineExecutor) {
    InlineExecutor exec;
    bool ran = false;

    auto task = runVoidTask([&]() -> Task<void> {
        ran = true;
        co_return;
    });

    task.getHandle().resume();
    EXPECT_TRUE(ran);
    EXPECT_TRUE(task.isDone());
}

TEST(TaskTest, MoveAssignmentChain) {
    auto t1 = simpleInt(1);
    auto t2 = simpleInt(2);
    auto t3 = simpleInt(3);

    t2 = std::move(t1);
    t3 = std::move(t2);

    t3.resume();
    EXPECT_EQ(t3.result(), 1);
}

TEST(TaskTest, NestedExceptionInChain) {
    auto task = []() -> Task<int> {
        int v = co_await []() -> Task<int> {
            int w = co_await []() -> Task<int> {
                throw std::runtime_error("deep");
                co_return 0;
            }();
            co_return w;
        }();
        co_return v;
    }();

    task.resume();
    EXPECT_THROW((void)task.result(), std::runtime_error);
}

TEST(TaskTest, ResumeDeepChain) {
    auto deepest = []() -> Task<int> {
        co_return 1;
    };

    auto middle = runIntTask([&]() -> Task<int> {
        auto v = co_await deepest();
        co_return v + 2;
    });

    auto top = runIntTask([middle = std::move(middle)]() mutable -> Task<int> {
        auto v = co_await std::move(middle);
        co_return v + 3;
    });

    auto t = std::move(top);
    t.resume();
    EXPECT_EQ(t.result(), 6);
}

TEST(TaskTest, TaskMovedAfterResume) {
    auto t1 = simpleInt(42);
    t1.resume();
    EXPECT_TRUE(t1.isDone());

    auto t2 = std::move(t1);
    EXPECT_FALSE(t1.isValid());  // moved-from invalid
    EXPECT_TRUE(t2.isValid());   // valid handle, coroutine already done
    EXPECT_TRUE(t2.isDone());
    EXPECT_EQ(t2.result(), 42);
}

TEST(SerialExecutorTest, LastOwnerMayBeReleasedByWorkerTask) {
    auto executor = std::make_shared<SerialExecutor>();
    auto releaseFirstTask = std::make_shared<std::promise<void>>();
    auto continueFirstTask = releaseFirstTask->get_future().share();
    auto secondTaskDone = std::make_shared<std::promise<void>>();
    auto secondTaskFuture = secondTaskDone->get_future();

    ASSERT_TRUE(executor->tryExecute(
        [keepAlive = executor, continueFirstTask]() mutable {
            continueFirstTask.wait();
            keepAlive.reset();
        }));
    ASSERT_TRUE(executor->tryExecute([secondTaskDone] { secondTaskDone->set_value(); }));

    executor.reset();
    releaseFirstTask->set_value();

    EXPECT_EQ(secondTaskFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready)
        << "the worker must finish draining its queue after its task releases the last executor owner";
}

namespace {

struct UncancellableWait {
    std::shared_ptr<std::shared_ptr<ResumeGate>> gateOut;

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        releaseTransferLeases(handle.promise());
        markExternalAwait(handle.promise());
        *gateOut = makeResumeGate(handle);
        return (*gateOut)->arm();
    }

    void await_resume() const noexcept {}
};

Task<void> parkUncancellable(std::shared_ptr<int> payload,
                             std::shared_ptr<std::shared_ptr<ResumeGate>> gateOut,
                             std::shared_ptr<bool> ranAfterWait) {
    co_await UncancellableWait{gateOut};
    *ranAfterWait = true;
    (void)payload;
}

}  // namespace

TEST(CoScopeTest, DestructorDoesNotWaitForChildThatIgnoresCancel) {
    auto gateOut = std::make_shared<std::shared_ptr<ResumeGate>>();
    auto ranAfterWait = std::make_shared<bool>(false);
    const auto start = std::chrono::steady_clock::now();
    {
        AsyncScope scope;
        scope.spawn(parkUncancellable(std::make_shared<int>(1), gateOut, ranAfterWait));
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1))
        << "the destructor must not block on the child";
    EXPECT_FALSE(*ranAfterWait) << "cancellation cannot wake the uncancellable child";
    ASSERT_TRUE(static_cast<bool>(*gateOut));

    (*gateOut)->signal();
    EXPECT_TRUE(*ranAfterWait) << "the detached child still runs to completion";
}

TEST(CoScopeTest, JoinWaitsForChildThatIgnoresCancel) {
    auto gateOut = std::make_shared<std::shared_ptr<ResumeGate>>();
    auto ranAfterWait = std::make_shared<bool>(false);
    AsyncScope scope;
    scope.spawn(parkUncancellable(std::make_shared<int>(1), gateOut, ranAfterWait));
    std::thread releaser([gateOut] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (*gateOut) {
            (*gateOut)->signal();
        }
    });
    ASSERT_TRUE(slark::test::joinScope(scope, std::chrono::seconds(5)));
    EXPECT_TRUE(*ranAfterWait) << "join returned before the child finished";
    releaser.join();
}

TEST(CoScopeTest, FinishedChildrenAreReclaimedAcrossSpawns) {
    AsyncScope scope;
    auto probe = std::make_shared<int>(0);
    for (int i = 0; i < 5000; ++i) {
        scope.spawn(runVoidTask([probe]() -> Task<void> {
            co_return;
        }));
    }
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
    EXPECT_EQ(probe.use_count(), 1);
}

TEST(CoScopeTest, ReclaimKeepsParkedChildrenAlive) {
    AsyncScope scope;
    std::vector<std::shared_ptr<std::shared_ptr<ResumeGate>>> gates;
    std::vector<std::shared_ptr<bool>> ranAfterWait;
    for (int i = 0; i < 3; ++i) {
        gates.push_back(std::make_shared<std::shared_ptr<ResumeGate>>());
        ranAfterWait.push_back(std::make_shared<bool>(false));
        scope.spawn(parkUncancellable(std::make_shared<int>(i), gates.back(), ranAfterWait.back()));
    }
    for (int i = 0; i < 1000; ++i) {
        scope.spawn(emptyTask());
    }
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 3u);

    for (const auto& gate : gates) {
        ASSERT_TRUE(static_cast<bool>(*gate));
        (*gate)->signal();
    }
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
    for (const auto& ran : ranAfterWait) {
        EXPECT_TRUE(*ran);
    }
    ASSERT_TRUE(slark::test::joinScope(scope));
}

TEST(CoScopeTest, CompletedChildrenLeaveTableWithoutNextCall) {
    auto executor = std::make_shared<SerialExecutor>();
    auto done = std::make_shared<std::atomic<int>>(0);
    AsyncScope scope;
    scope.setDefaultExecutor(*executor);
    constexpr int kSpawns = 200;
    for (int i = 0; i < kSpawns; ++i) {
        scope.spawn(runVoidTask([done]() -> Task<void> {
            done->fetch_add(1, std::memory_order_relaxed);
            co_return;
        }));
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (AsyncScopeTestAccess::trackedChildren(scope) != 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(done->load(), kSpawns);
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
}

TEST(CoScopeTest, SpawnReclaimRacesWithChildCompletionOnWorker) {
    auto executor = std::make_shared<SerialExecutor>();
    auto done = std::make_shared<std::atomic<int>>(0);
    AsyncScope scope;
    scope.setDefaultExecutor(*executor);
    constexpr int kSpawns = 3000;
    for (int i = 0; i < kSpawns; ++i) {
        scope.spawn(runVoidTask([done]() -> Task<void> {
            co_await simpleInt(1);
            done->fetch_add(1, std::memory_order_relaxed);
        }));
    }
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_EQ(done->load(), kSpawns);
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
}

TEST(CoScopeTest, ReclaimRacesWithExecutorCompletions) {
    auto executor = std::make_shared<SerialExecutor>();
    AsyncScope scope;
    scope.setDefaultExecutor(*executor);
    for (int i = 0; i < 2000; ++i) {
        scope.spawn(emptyTask());
    }
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
}

namespace {

Task<void> markRanOnce(std::shared_ptr<std::atomic<int>> ran) {
    ran->fetch_add(1);
    co_return;
}

Task<void> parkThenMarkRan(std::shared_ptr<CompletionAwaiter<void>> gate,
                           std::shared_ptr<std::atomic<int>> ran) {
    co_await *gate;
    ran->fetch_add(1);
}

}  // namespace

TEST(CoScopeTest, SpawnOfAnEmptyTaskThrowsAndRegistersNothing) {
    AsyncScope scope;
    EXPECT_THROW(scope.spawn(Task<void>{}), std::logic_error);
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
    EXPECT_FALSE(scope.token().isCancelled())
        << "a rejected spawn is a caller error, not a scope failure";
    EXPECT_TRUE(slark::test::joinScope(scope)) << "nothing was registered, so join returns";
}

TEST(CoScopeTest, SpawnOfAnEmptyTaskWithExplicitTokenThrowsAndRegistersNothing) {
    AsyncScope scope;
    CancellationToken own;
    EXPECT_THROW(scope.spawn(Task<void>{}, own), std::logic_error);
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
    EXPECT_FALSE(scope.token().isCancelled());
}

TEST(CoScopeTest, SpawnOfAStartedSuspendedTaskThrowsAndNeverResumesIt) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto ran = std::make_shared<std::atomic<int>>(0);
    Task<void> child = parkThenMarkRan(gate, ran);
    child.resume();
    ASSERT_FALSE(child.isDone());

    AsyncScope scope;
    EXPECT_THROW(scope.spawn(std::move(child)), std::logic_error)
        << "a started child is parked on its own wait; the scope must not enter it";
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
    EXPECT_FALSE(scope.token().isCancelled())
        << "the scope must not fail because of a rejected spawn";

    gate->complete();
    EXPECT_EQ(ran->load(), 0) << "the rejected child is released and must not run on";
    EXPECT_TRUE(slark::test::joinScope(scope));
}

TEST(CoScopeTest, SpawnOfACompletedTaskThrows) {
    auto ran = std::make_shared<std::atomic<int>>(0);
    Task<void> child = markRanOnce(ran);
    child.resume();
    ASSERT_TRUE(child.isDone());

    AsyncScope scope;
    EXPECT_THROW(scope.spawn(std::move(child)), std::logic_error);
    EXPECT_EQ(ran->load(), 1) << "the child ran once, before it was offered to the scope";
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
}

TEST(CoScopeTest, RefusedSpawnReturnsFalseAndNeverRuns) {
    auto executor = std::make_shared<SerialExecutor>();
    AsyncScope scope(executor);
    executor->requestStop();
    auto ran = std::make_shared<std::atomic<int>>(0);
    EXPECT_FALSE(scope.spawn(markRanOnce(ran)))
        << "a stopped domain refuses the child, which must not start";
    EXPECT_TRUE(slark::test::joinScope(scope)) << "a refused child leaves no join obligation";
    EXPECT_EQ(ran->load(), 0);
    EXPECT_EQ(AsyncScopeTestAccess::trackedChildren(scope), 0u);
}

TEST(CoScopeTest, RefusedSpawnWithExplicitTokenReturnsFalseAndNeverRuns) {
    auto executor = std::make_shared<SerialExecutor>();
    AsyncScope scope(executor);
    executor->requestStop();
    auto ran = std::make_shared<std::atomic<int>>(0);
    CancellationToken own;
    EXPECT_FALSE(scope.spawn(markRanOnce(ran), own));
    EXPECT_TRUE(slark::test::joinScope(scope));
    EXPECT_EQ(ran->load(), 0);
}

TEST(CoScopeTest, RefusedSpawnDoesNotCancelTheScope) {
    auto executor = std::make_shared<SerialExecutor>();
    AsyncScope scope(executor);
    executor->requestStop();
    auto refused = std::make_shared<std::atomic<int>>(0);
    EXPECT_FALSE(scope.spawn(markRanOnce(refused)));
    EXPECT_FALSE(scope.token().isCancelled())
        << "refusal is a per-child outcome, not a scope failure";
}

TEST(CoScopeTest, ConstructorWithNullExecutorRunsChildrenInline) {
    AsyncScope scope(nullptr);
    auto ran = std::make_shared<std::atomic<int>>(0);
    ASSERT_TRUE(scope.spawn(markRanOnce(ran)));
    EXPECT_EQ(ran->load(), 1) << "without a domain the child starts on the spawning thread";
    EXPECT_TRUE(slark::test::joinScope(scope));
}

TEST(CoScopeTest, ConstructorExecutorOutlivesTheScopeReference) {
    auto executor = std::make_shared<SerialExecutor>();
    std::weak_ptr<SerialExecutor> watch = executor;
    {
        AsyncScope scope(executor);
        executor.reset();
        auto ran = std::make_shared<std::atomic<int>>(0);
        ASSERT_TRUE(scope.spawn(markRanOnce(ran)));
        ASSERT_TRUE(slark::test::joinScope(scope));
        EXPECT_EQ(ran->load(), 1);
        EXPECT_FALSE(watch.expired()) << "the scope keeps its owned domain alive";
    }
    EXPECT_TRUE(watch.expired()) << "destroying the scope releases the owned domain";
}

TEST(CoScopeTest, SetDefaultExecutorReplacesTheDomainForLaterChildren) {
    auto first = std::make_shared<SerialExecutor>();
    auto second = std::make_shared<SerialExecutor>();
    AsyncScope scope;
    scope.setDefaultExecutor(*first);
    scope.setDefaultExecutor(*second);
    auto ran = std::make_shared<std::atomic<int>>(0);
    ASSERT_TRUE(scope.spawn(markRanOnce(ran)));
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_EQ(ran->load(), 1);
}


namespace {

Task<void> awaitGateRecordingCancel(std::shared_ptr<CompletionAwaiter<void>> gate,
                                    std::shared_ptr<std::atomic<bool>> cancelled) {
    try {
        co_await *gate;
    } catch (const CancelledException&) {
        cancelled->store(true);
    }
}

Task<void> reportThreadOnce(std::shared_ptr<std::promise<std::thread::id>> out) {
    out->set_value(std::this_thread::get_id());
    co_return;
}

}  // namespace

TEST(CoScopeTest, SpawnWithExplicitTokenIsCancelledByScope) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    AsyncScope scope;
    CancellationToken own;
    ASSERT_TRUE(scope.spawn(awaitGateRecordingCancel(gate, cancelled), own));

    scope.cancelAll();
    EXPECT_TRUE(cancelled->load()) << "cancelAll must reach a child that also carries its own token";
    gate->complete();
    EXPECT_TRUE(slark::test::joinScope(scope));
}

TEST(CoScopeTest, SpawnWithExplicitTokenIsCancelledByItsOwnToken) {
    auto gate = std::make_shared<CompletionAwaiter<void>>();
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    AsyncScope scope;
    CancellationToken own;
    ASSERT_TRUE(scope.spawn(awaitGateRecordingCancel(gate, cancelled), own));

    own.cancel();
    EXPECT_TRUE(cancelled->load());
    gate->complete();
    EXPECT_TRUE(slark::test::joinScope(scope));
}

TEST(CoScopeTest, SetDefaultExecutorAfterSpawnThrows) {
    auto executor = std::make_shared<SerialExecutor>();
    AsyncScope scope;
    scope.spawn(emptyTask());
    EXPECT_THROW(scope.setDefaultExecutor(*executor), std::logic_error)
        << "children spawned earlier already chose their domain";
    EXPECT_TRUE(slark::test::joinScope(scope));
}

TEST(CoScopeTest, ConstructorExecutorRunsChildrenOnItsDomain) {
    auto executor = std::make_shared<SerialExecutor>();
    std::promise<std::thread::id> workerProbe;
    auto workerFuture = workerProbe.get_future();
    executor->execute([p = std::move(workerProbe)]() mutable {
        p.set_value(std::this_thread::get_id());
    });
    const auto worker = workerFuture.get();

    auto ranOn = std::make_shared<std::promise<std::thread::id>>();
    auto ranOnFuture = ranOn->get_future();
    AsyncScope scope(executor);
    ASSERT_TRUE(scope.spawn(reportThreadOnce(ranOn)));
    ASSERT_TRUE(slark::test::joinScope(scope));
    ASSERT_EQ(ranOnFuture.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(ranOnFuture.get(), worker)
        << "a scope constructed with a domain must run its children on that domain";
}
