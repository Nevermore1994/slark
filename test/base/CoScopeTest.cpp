// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "CoScope.hpp"

#include <gtest/gtest.h>

#include <memory>

#include "Awaitable.hpp"
#include "CoWhenAll.hpp"
#include "Executor.h"
#include "Task.hpp"

using namespace slark;
using namespace slark::coro;

Task<int> simpleInt(int v) {
    co_return v;
}

Task<void> emptyTask() {
    co_return;
}

TEST(CoScopeTest, SpawnAndWait) {
    AsyncScope scope;
    int result = 0;

    scope.spawn([&result]() -> Task<void> {
        int v = co_await simpleInt(42);
        result = v;
    }());

    scope.waitAll();
    EXPECT_EQ(result, 42);
}

TEST(CoScopeTest, MultipleSpawns) {
    AsyncScope scope;
    std::atomic<int> counter{0};

    for (int i = 0; i < 10; ++i) {
        scope.spawn([&counter, i]() -> Task<void> {
            auto v = co_await simpleInt(i);
            counter.fetch_add(v);
        }());
    }

    scope.waitAll();
    EXPECT_EQ(counter.load(), 45);
}

TEST(CoScopeTest, CancelAll) {
    AsyncScope scope;
    std::atomic<bool> ran{false};

    scope.spawn([&ran]() -> Task<void> {
        ran = true;
        co_return;
    }());

    scope.cancelAll();
    scope.waitAll();
}

TEST(CoScopeTest, CancelBeforeWait) {
    AsyncScope scope;

    scope.cancelAll();
    scope.waitAll();
}

TEST(CoScopeTest, DestroyWithBlockedChildIsSafe) {
    // C-07：child 挂起在不可取消的 awaiter 上时，scope 析构不得 UAF。
    // 超时后保留 frame 由 child 保活，测试本身只验证不崩溃。
    auto blocker = std::make_shared<CompletionAwaiter<void>>();
    {
        AsyncScope scope;
        scope.spawn([&blocker]() -> Task<void> {
            co_await *blocker;
        }());
        // scope 析构触发 cancel + 5s 等待，超时后保留 frame
    }
    // 之后唤醒 child（frame 仍存活），不崩溃即可
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

    auto task = [&]() -> Task<void> {
        co_await when_all(
            [&a]() -> Task<void> {
                a = true;
                co_return;
            }(),
            [&b]() -> Task<void> {
                b = true;
                co_return;
            }());
        co_return;
    }();

    task.resume();
    EXPECT_TRUE(a);
    EXPECT_TRUE(b);
}

TEST(CoWhenAllTest, MixedTasksSeparate) {
    int intResult = 0;
    bool voidResult = false;

    auto task = [&]() -> Task<void> {
        co_await when_all(
            [&intResult]() -> Task<void> {
                intResult = co_await simpleInt(42);
            }(),
            [&voidResult]() -> Task<void> {
                voidResult = true;
                co_return;
            }());
        co_return;
    }();

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
    auto task = [&]() -> Task<int> {
        auto [a, b] = co_await when_all(*exec, simpleInt(5), simpleInt(15));
        co_return a + b;
    }();

    task.resume();
    EXPECT_EQ(task.result(), 20);
}

TEST(CoWhenAllTest, CoWhenAllInScope) {
    AsyncScope scope;
    int result = 0;

    scope.spawn([&result]() -> Task<void> {
        auto [a, b] = co_await when_all(simpleInt(7), simpleInt(8));
        result = a + b;
    }());

    scope.waitAll();
    EXPECT_EQ(result, 15);
}

TEST(TaskTest, TaskWithInlineExecutor) {
    InlineExecutor exec;
    bool ran = false;

    auto task = [&]() -> Task<void> {
        ran = true;
        co_return;
    }();

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

    auto middle = [&]() -> Task<int> {
        auto v = co_await deepest();
        co_return v + 2;
    };

    auto top = [&]() -> Task<int> {
        auto v = co_await middle();
        co_return v + 3;
    };

    auto t = top();
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
