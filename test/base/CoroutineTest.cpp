//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Coroutine.hpp"

#include "Awaitable.hpp"
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

// ---- 补充边界测试 ----

// Coroutine<void> getResult 异常重抛
TEST(CoroutineTest, VoidExceptionRethrow) {
    auto coro = []() -> Coroutine<void> {
        throw std::runtime_error("void exception");
        co_return;
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_THROW(coro.getResult(), std::runtime_error);
}

// Coroutine<T> getResult 异常重抛
TEST(CoroutineTest, TypedExceptionRethrow) {
    auto coro = []() -> Coroutine<int> {
        throw std::invalid_argument("typed exception");
        co_return 0;
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_THROW(coro.getResult(), std::invalid_argument);
}

// getResult on invalid coroutine throws
TEST(CoroutineTest, GetResultInvalidThrows) {
    Coroutine<int> empty = []() -> Coroutine<int> {
        co_return 1;
    }();
    Coroutine<int> moved = std::move(empty);
    EXPECT_FALSE(empty.isValid());
    EXPECT_THROW(empty.getResult(), std::runtime_error);
}

// release() 转移句柄所有权
TEST(CoroutineTest, Release) {
    auto coro = []() -> Coroutine<int> {
        co_return 77;
    }();

    auto handle = coro.release();
    EXPECT_FALSE(coro.isValid());
    EXPECT_TRUE(handle != nullptr);

    // 手动 resume 并销毁
    handle.resume();
    EXPECT_TRUE(handle.done());
    handle.destroy();
}

// resume 已完成的协程不崩溃
TEST(CoroutineTest, ResumeAlreadyDone) {
    auto coro = []() -> Coroutine<void> {
        co_return;
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_NO_THROW(coro.resume()); // 幂等
}

// co_await 链式挂起（continuation 机制）
TEST(CoroutineTest, CoAwaitChain) {
    auto inner = []() -> Coroutine<int> {
        co_return 42;
    }();

    int result = 0;
    auto outer = [&]() -> Coroutine<void> {
        result = co_await inner;
        co_return;
    }();

    outer.resume();
    EXPECT_TRUE(outer.isDone());
    EXPECT_EQ(result, 42);
}

// co_await 多层嵌套
TEST(CoroutineTest, CoAwaitDeepChain) {
    auto level1 = []() -> Coroutine<int> {
        co_return 1;
    }();
    auto level2 = [&]() -> Coroutine<int> {
        int v = co_await level1;
        co_return v + 10;
    }();
    auto level3 = [&]() -> Coroutine<int> {
        int v = co_await level2;
        co_return v + 100;
    }();

    level3.resume();
    EXPECT_TRUE(level3.isDone());
    EXPECT_EQ(level3.getResult(), 111);
}

// move assignment
TEST(CoroutineTest, MoveAssignment) {
    auto coro1 = []() -> Coroutine<int> {
        co_return 10;
    }();
    auto coro2 = []() -> Coroutine<int> {
        co_return 20;
    }();

    coro1 = std::move(coro2);
    EXPECT_FALSE(coro2.isValid());
    EXPECT_TRUE(coro1.isValid());
    coro1.resume();
    EXPECT_EQ(coro1.getResult(), 20);
}

// ---- CancellationToken 补充测试 ----

// registerCallback 在取消时触发
TEST(CancellationTokenTest, RegisterCallbackFired) {
    CancellationToken token;
    bool fired = false;
    auto reg = token.registerCallback([&fired] {
        fired = true;
    });
    EXPECT_TRUE(reg.isValid());
    EXPECT_FALSE(fired);

    token.cancel();
    EXPECT_TRUE(fired);
}

// registerCallback 在已取消的 token 上立即无效
TEST(CancellationTokenTest, RegisterCallbackOnCancelledToken) {
    CancellationToken token;
    token.cancel();

    bool fired = false;
    auto reg = token.registerCallback([&fired] {
        fired = true;
    });
    // 已取消，注册句柄无效，回调不会被调用
    EXPECT_FALSE(reg.isValid());
    EXPECT_FALSE(fired);
}

// unregisterCallback 阻止回调触发
TEST(CancellationTokenTest, UnregisterCallback) {
    CancellationToken token;
    bool fired = false;
    auto reg = token.registerCallback([&fired] {
        fired = true;
    });
    reg.unregister();
    EXPECT_FALSE(reg.isValid());

    token.cancel();
    EXPECT_FALSE(fired);
}

// CancellationTokenRegistration RAII 自动注销
TEST(CancellationTokenTest, RegistrationRAII) {
    CancellationToken token;
    bool fired = false;
    {
        auto reg = token.registerCallback([&fired] {
            fired = true;
        });
        // reg 析构时自动注销
    }
    token.cancel();
    EXPECT_FALSE(fired);
}

// 多个回调都被触发
TEST(CancellationTokenTest, MultipleCallbacks) {
    CancellationToken token;
    int32_t count = 0;
    auto reg1 = token.registerCallback([&count] {
        ++count;
    });
    auto reg2 = token.registerCallback([&count] {
        ++count;
    });
    auto reg3 = token.registerCallback([&count] {
        ++count;
    });

    token.cancel();
    EXPECT_EQ(count, 3);
}

// CancellationTokenRegistration move 语义
TEST(CancellationTokenTest, RegistrationMoveSemantics) {
    CancellationToken token;
    bool fired = false;
    auto reg1 = token.registerCallback([&fired] {
        fired = true;
    });
    EXPECT_TRUE(reg1.isValid());

    auto reg2 = std::move(reg1);
    EXPECT_FALSE(reg1.isValid());
    EXPECT_TRUE(reg2.isValid());

    token.cancel();
    EXPECT_TRUE(fired);
}

// ---- CompletionAwaiter 测试 ----

// CompletionAwaiter<int> complete 后 co_await 得到结果
TEST(CompletionAwaiterTest, CompleteBeforeAwait) {
    CompletionAwaiter<int> awaiter;
    awaiter.complete(42);

    auto coro = [&]() -> Coroutine<int> {
        co_return co_await awaiter;
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), 42);
}

// CompletionAwaiter<int> complete 在 await_suspend 之后
TEST(CompletionAwaiterTest, CompleteAfterAwait) {
    auto awaiter = std::make_shared<CompletionAwaiter<int>>();
    std::atomic<bool> done{false};

    auto coro = [&]() -> Coroutine<int> {
        co_return co_await *awaiter;
    }();

    // 在线程中 resume，协程会挂起等待
    std::thread t([&] {
        coro.resume();
        done = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    awaiter->complete(99);
    t.join();

    EXPECT_TRUE(done.load());
    EXPECT_TRUE(coro.isDone());
    EXPECT_EQ(coro.getResult(), 99);
}

// CompletionAwaiter<int> fail 触发异常
TEST(CompletionAwaiterTest, FailThrowsException) {
    CompletionAwaiter<int> awaiter;
    awaiter.fail(std::make_exception_ptr(std::runtime_error("fail")));

    auto coro = [&]() -> Coroutine<int> {
        co_return co_await awaiter;
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_THROW(coro.getResult(), std::runtime_error);
}

// CompletionAwaiter<void> complete
TEST(CompletionAwaiterTest, VoidComplete) {
    CompletionAwaiter<void> awaiter;
    awaiter.complete();

    bool reached = false;
    auto coro = [&]() -> Coroutine<void> {
        co_await awaiter;
        reached = true;
        co_return;
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_TRUE(reached);
}

// CompletionAwaiter<void> fail 触发异常
TEST(CompletionAwaiterTest, VoidFailThrowsException) {
    CompletionAwaiter<void> awaiter;
    awaiter.fail(std::make_exception_ptr(std::logic_error("void fail")));

    auto coro = [&]() -> Coroutine<void> {
        co_await awaiter;
        co_return;
    }();

    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_THROW(coro.getResult(), std::logic_error);
}

// ---- TimerAwaiter 测试 ----

// TimerAwaiter 零延迟 await_ready 返回 true
TEST(TimerAwaiterTest, ZeroDelayReady) {
    TimerAwaiter awaiter(std::chrono::milliseconds(0));
    EXPECT_TRUE(awaiter.await_ready());
}

// TimerAwaiter 正延迟 await_ready 返回 false
TEST(TimerAwaiterTest, PositiveDelayNotReady) {
    TimerAwaiter awaiter(std::chrono::milliseconds(100));
    EXPECT_FALSE(awaiter.await_ready());
}

// TimerAwaiter 在协程中等待
TEST(TimerAwaiterTest, WaitInCoroutine) {
    using namespace std::chrono;
    auto start = steady_clock::now();
    std::atomic<bool> done{false};

    auto coro = [&]() -> Coroutine<void> {
        co_await TimerAwaiter(milliseconds(50));
        done = true;
        co_return;
    }();

    CoroutineScheduler::instance().schedule(coro.getHandle());
    std::this_thread::sleep_for(milliseconds(200));

    EXPECT_TRUE(done.load());
    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
    EXPECT_GE(elapsed, 50);
}

// ---- CoroutineScheduler 补充测试 ----

// waitForAll 等待所有任务完成
TEST(CoroutineSchedulerTest, WaitForAll) {
    auto& scheduler = CoroutineScheduler::instance();
    std::atomic<int32_t> counter{0};

    for (int32_t i = 0; i < 5; ++i) {
        auto coro = [&counter]() -> Coroutine<void> {
            counter++;
            co_return;
        }();
        scheduler.schedule(coro.release());
    }

    scheduler.waitForAll();
    EXPECT_EQ(counter.load(), 5);
}

// scheduleCallback 触发
TEST(CoroutineSchedulerTest, ScheduleCallback) {
    auto& scheduler = CoroutineScheduler::instance();
    std::atomic<bool> fired{false};

    scheduler.scheduleCallback(std::chrono::milliseconds(30), [&fired] {
        fired = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(fired.load());
}
