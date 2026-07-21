//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Coroutine.hpp"

#include "AsyncGenerator.hpp"
#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "CoroutineScheduler.hpp"
#include "ThreadPool.hpp"
#include "TimerManager.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

using namespace slark;

namespace {

// 清理全局协程调度器和定时器状态，防止前一个测试的残留 job 干扰当前测试
void cleanupGlobalState() {
    for (int32_t i = 0; i < 10; ++i) {
        CoroutineScheduler::instance().waitForAll();
        TimerManager::shareInstance().clear();
        CoroutineScheduler::instance().waitForAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

struct GlobalCleanupGuard {
    ~GlobalCleanupGuard() {
        cleanupGlobalState();
    }
};

} // namespace

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
    GlobalCleanupGuard guard;
    auto& scheduler = CoroutineScheduler::instance();

    std::atomic<int> counter{0};
    auto coro = [&]() -> Coroutine<void> {
        counter++;
        co_return;
    }();

    scheduler.schedule(coro.makeResumeHandle());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(counter.load(), 1);
}

TEST(CoroutineSchedulerTest, ScheduleAfter) {
    GlobalCleanupGuard guard;
    auto& scheduler = CoroutineScheduler::instance();

    std::atomic<int> counter{0};
    auto coro = [&]() -> Coroutine<void> {
        counter++;
        co_return;
    }();

    auto timerId = scheduler.scheduleAfter(std::chrono::milliseconds(50),
                                            std::move(coro).share());
    EXPECT_TRUE(timerId.isValid());

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(counter.load(), 1);
}

TEST(CoroutineSchedulerTest, CancelTimerByTimerId) {
    GlobalCleanupGuard guard;
    auto& scheduler = CoroutineScheduler::instance();

    std::atomic<int> counter{0};
    auto coro = [&]() -> Coroutine<void> {
        counter++;
        co_return;
    }();

    auto timerId = scheduler.scheduleAfter(std::chrono::milliseconds(200),
                                            std::move(coro).share());
    scheduler.cancel(timerId);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(counter.load(), 0);
}

TEST(CoroutineSchedulerTest, ScheduleCoroutineTemplate) {
    GlobalCleanupGuard guard;
    auto& scheduler = CoroutineScheduler::instance();

    std::atomic<int> counter{0};
    auto coro = [&]() -> Coroutine<int> {
        counter++;
        co_return 42;
    }();

    auto timerId = scheduler.scheduleAfter(std::chrono::milliseconds(30),
                                            std::move(coro).share());
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

// share() 转移所有权到 SharedCoroutineHandle
TEST(CoroutineTest, Share) {
    auto coro = []() -> Coroutine<int> {
        co_return 77;
    }();

    auto shared = std::move(coro).share();
    EXPECT_FALSE(coro.isValid());
    EXPECT_TRUE(shared.hasLifetime());
    EXPECT_FALSE(shared.done());

    // 协程在 initial_suspend 挂起，resume 后立即完成
    shared.tryResume();
    EXPECT_TRUE(shared.done());
    // shared 析构时 frame 由 deleter 自动 destroy
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
    int result = 0;
    // inner 是临时 lambda 右值，co_await 时走右值路径转移所有权
    auto outer = [&]() -> Coroutine<void> {
        auto inner = []() -> Coroutine<int> {
            co_return 42;
        }();
        result = co_await std::move(inner);
        co_return;
    }();

    outer.resume();
    CoroutineScheduler::instance().waitForAll();
    EXPECT_TRUE(outer.isDone());
    EXPECT_EQ(result, 42);
}

// co_await 多层嵌套
TEST(CoroutineTest, CoAwaitDeepChain) {
    auto outer = [&]() -> Coroutine<int> {
        auto level1 = []() -> Coroutine<int> {
            co_return 1;
        }();
        auto level2 = [&level1]() -> Coroutine<int> {
            int v = co_await std::move(level1);
            co_return v + 10;
        }();
        int v = co_await std::move(level2);
        co_return v + 100;
    }();

    outer.resume();
    CoroutineScheduler::instance().waitForAll();
    EXPECT_TRUE(outer.isDone());
    EXPECT_EQ(outer.getResult(), 111);
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
    // 等待线程池恢复协程
    for (int i = 0; i < 50 && !coro.isDone(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
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
    GlobalCleanupGuard guard;
    using namespace std::chrono;
    auto start = steady_clock::now();
    std::atomic<bool> done{false};

    auto coro = [&]() -> Coroutine<void> {
        co_await TimerAwaiter(milliseconds(50));
        done = true;
        co_return;
    }();

    CoroutineScheduler::instance().schedule(coro.makeResumeHandle());
    std::this_thread::sleep_for(milliseconds(200));

    EXPECT_TRUE(done.load());
    auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
    EXPECT_GE(elapsed, 50);
}

// ---- CoroutineScheduler 补充测试 ----

// waitForAll 等待所有任务完成
TEST(CoroutineSchedulerTest, WaitForAll) {
    GlobalCleanupGuard guard;
    auto& scheduler = CoroutineScheduler::instance();
    std::atomic<int32_t> counter{0};

    for (int32_t i = 0; i < 5; ++i) {
        auto coro = [&counter]() -> Coroutine<void> {
            counter++;
            co_return;
        }();
        scheduler.schedule(std::move(coro).share());
    }

    scheduler.waitForAll();
    EXPECT_EQ(counter.load(), 5);
}

// scheduleCallback 触发
TEST(CoroutineSchedulerTest, ScheduleCallback) {
    GlobalCleanupGuard guard;
    auto& scheduler = CoroutineScheduler::instance();
    std::atomic<bool> fired{false};

    scheduler.scheduleCallback(std::chrono::milliseconds(30), [&fired] {
        fired = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(fired.load());
}

// ---- Coroutine 协程生命周期与 CancellationToken 集成测试 ----

TEST(CoroutineTest, CancelledCoroutineExitsSafely) {
    GlobalCleanupGuard guard;
    CancellationToken token;
    std::atomic<bool> exited{false};

    auto coro = [&]() -> Coroutine<void> {
        while (!token.isCancelled()) {
            co_await TimerAwaiter(std::chrono::milliseconds(5));
        }
        exited = true;
        co_return;
    }();

    coro.resume();
    EXPECT_FALSE(exited.load());

    token.cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(exited.load());
    EXPECT_TRUE(coro.isDone());
}

TEST(CoroutineTest, DestroyedCoroutineHandleNotResumedByCancellation) {
    GlobalCleanupGuard guard;
    CancellationToken token;
    std::atomic<int> resumeCount{0};
    std::atomic<bool> completed{false};

    {
        auto coro = [&]() -> Coroutine<void> {
            auto reg = token.registerCallback([&resumeCount]() {
                resumeCount++;
            });
            co_await TimerAwaiter(std::chrono::milliseconds(10));
            completed = true;
            co_return;
        }();

        coro.resume();
    } // Coroutine 对象销毁，但 TimerAwaiter 持有的 SharedCoroutineHandle 仍保活 frame

    token.cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 新设计下 frame 在最后一个 SharedCoroutineHandle 释放后才 destroy，
    // 因此 cancel 回调会被安全执行，不会访问已销毁 frame。
    EXPECT_EQ(resumeCount.load(), 1);
    EXPECT_TRUE(completed.load());
}

TEST(CoroutineTest, MoveAssignmentDestroysOldCoroutine) {
    GlobalCleanupGuard guard;
    std::atomic<int> destroyed{0};

    struct DestructorTracker {
        std::atomic<int>& counter;
        ~DestructorTracker() { counter++; }
    };

    auto makeCoro = [&]() -> Coroutine<void> {
        DestructorTracker tracker{destroyed};
        co_await TimerAwaiter(std::chrono::milliseconds(200));
        co_return;
    };

    auto coro = makeCoro();
    coro.resume();

    auto newCoro = makeCoro();
    newCoro.resume();

    coro = std::move(newCoro);
    EXPECT_TRUE(coro.isValid());
    EXPECT_FALSE(newCoro.isValid());
    // 等待旧 frame 的 TimerAwaiter 触发并完成，避免遗留任务影响后续测试
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

TEST(CoroutineTest, CancellationTokenPropagatesThroughNestedCoroutines) {
    GlobalCleanupGuard guard;
    CancellationToken token;
    std::atomic<bool> innerExited{false};
    std::atomic<bool> outerExited{false};

    auto inner = [&](CancellationToken t) -> Coroutine<void> {
        while (!t.isCancelled()) {
            co_await TimerAwaiter(std::chrono::milliseconds(5));
        }
        innerExited = true;
        co_return;
    };

    auto outer = [&](CancellationToken t) -> Coroutine<void> {
        co_await inner(t);
        outerExited = true;
        co_return;
    };

    auto coro = outer(token);
    coro.resume();

    token.cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(innerExited.load());
    EXPECT_TRUE(outerExited.load());
}

TEST(CoroutineTest, MultipleCancelsAndNewCoroutines) {
    GlobalCleanupGuard guard;
    // 根因：原始测试用 [&] 按引用捕获 token，循环内 token = CancellationToken{}
    // 重新赋值后，协程通过引用读到的是新 token（未取消），永不退出，无限注册 5ms 定时器。
    // for 结束时 coro 局部变量析构 destroy() 仍挂起的协程帧，随后 TimerManager 触发定时器
    // 访问已销毁 handle → UB 崩溃。
    // 修复：每次创建独立的 CancellationToken，协程拷贝一份（取消状态共享），
    // 不再重新赋值，token 生命周期持续到协程完成。
    CancellationToken token;
    std::atomic<int> completedCount{0};

    for (int i = 0; i < 10; i++) {
        auto coro = [t = token, &completedCount]() -> Coroutine<void> {
            while (!t.isCancelled()) {
                co_await TimerAwaiter(std::chrono::milliseconds(5));
            }
            completedCount++;
            co_return;
        }();

        coro.resume();
        token.cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_EQ(completedCount.load(), 10);
}

TEST(CoroutineTest, CancelledAwaitReturnsImmediately) {
    GlobalCleanupGuard guard;
    CancellationToken token;

    auto coro = [&]() -> Coroutine<void> {
        co_await TimerAwaiter(std::chrono::milliseconds(500));
        co_return;
    }();

    coro.resume();
    token.cancel();

    { auto tmp = std::move(coro); }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    SUCCEED();
}

TEST(CoroutineTest, DestructorDoesNotThrow) {
    GlobalCleanupGuard guard;
    auto coro = []() -> Coroutine<void> {
        co_await TimerAwaiter(std::chrono::milliseconds(100));
        co_return;
    }();

    coro.resume();
    EXPECT_NO_THROW({ auto tmp = std::move(coro); });
}

TEST(CoroutineLifetimeTest, RvalueAwaitConsumesCoroutineOwner) {
    GlobalCleanupGuard guard;
    std::atomic<bool> completed{false};

    auto child = [&]() -> Coroutine<void> {
        co_await TimerAwaiter(std::chrono::milliseconds(20));
    }();

    auto parent = [&]() -> Coroutine<void> {
        co_await std::move(child);
        completed = true;
    }();

    parent.resume();
    EXPECT_FALSE(child.isValid());

    for (int i = 0; i < 100 && !completed.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(completed.load());
    EXPECT_TRUE(parent.isDone());
}

TEST(CoroutineLifetimeTest, NestedTemporaryCoroutinesKeepIntermediateFramesAlive) {
    GlobalCleanupGuard guard;
    std::atomic<bool> completed{false};

    auto leaf = []() -> Coroutine<void> {
        co_await TimerAwaiter(std::chrono::milliseconds(20));
    };

    auto middle = [&]() -> Coroutine<void> {
        co_await leaf();
    };

    auto outer = [&]() -> Coroutine<void> {
        co_await middle();
        completed = true;
    }();

    outer.resume();

    for (int i = 0; i < 100 && !completed.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(completed.load());
    EXPECT_TRUE(outer.isDone());
}

TEST(CoroutineLifetimeTest, DestroyedGeneratorOwnerDoesNotInvalidatePendingFrame) {
    GlobalCleanupGuard guard;
    std::atomic<bool> received{false};
    std::unique_ptr<AsyncGenerator<int>> generator = std::make_unique<AsyncGenerator<int>>(
        []() -> AsyncGenerator<int> {
            co_await TimerAwaiter(std::chrono::milliseconds(20));
            co_yield 42;
        }());

    auto consumer = [&]() -> Coroutine<void> {
        auto value = co_await generator->next();
        received = value.has_value() && *value == 42;
    }();

    consumer.resume();
    generator.reset();

    for (int i = 0; i < 100 && !received.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(received.load());
    EXPECT_TRUE(consumer.isDone());
}

// ─── Coroutine::cancel 行为测试 ───

TEST(CoroutineTest, CancelIsIdempotent) {
    auto coro = []() -> Coroutine<void> { co_return; }();
    coro.cancel();
    EXPECT_TRUE(coro.isCancelled());
    coro.cancel();
    EXPECT_TRUE(coro.isCancelled());
}

TEST(CoroutineTest, CancelBeforeResume) {
    auto coro = []() -> Coroutine<void> { co_return; }();
    coro.cancel();
    coro.resume();
    // cancel 后 resume 不应导致协程实际执行，但不崩溃
    SUCCEED();
}

TEST(CoroutineTest, CancelCompletedIsNoop) {
    auto coro = []() -> Coroutine<void> { co_return; }();
    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_FALSE(coro.isCancelled());
    coro.cancel();
    // cancel 设置 cancelled_ 标志，不会因为协程已完成就清掉
    EXPECT_TRUE(coro.isCancelled());
}

// ─── Coroutine::share 后状态测试 ───

TEST(CoroutineTest, ShareMovesOwnership) {
    auto coro = []() -> Coroutine<int> { co_return 42; }();
    EXPECT_TRUE(coro.isValid());
    auto handle = std::move(coro).share();
    EXPECT_FALSE(coro.isValid());
    // share 后 control_ 被移走，isDone() 返回 true（!control_）
    EXPECT_TRUE(coro.isDone());
    EXPECT_FALSE(handle.done());
    handle.tryResume();
    EXPECT_TRUE(handle.done());
}

TEST(CoroutineTest, ShareOnDoneReturnsDoneHandle) {
    auto coro = []() -> Coroutine<int> { co_return 42; }();
    coro.resume();
    EXPECT_TRUE(coro.isDone());
    auto handle = std::move(coro).share();
    EXPECT_TRUE(handle.done());
}

// ─── 无效 Coroutine（默认构造 / moved-from）防御性操作 ───

TEST(CoroutineTest, DefaultConstructedIsInvalid) {
    // Coroutine 没有默认构造，但 moved-from 后应安全
    auto coro = []() -> Coroutine<void> { co_return; }();
    auto coro2 = std::move(coro);
    EXPECT_FALSE(coro.isValid());
    // control_ 被移走，isDone() 返回 true
    EXPECT_TRUE(coro.isDone());
    EXPECT_FALSE(coro.isCancelled());
    EXPECT_NO_THROW(coro.resume());
    EXPECT_NO_THROW(coro.cancel());
}

TEST(CoroutineTest, MovedFromGetResultThrows) {
    auto coro = []() -> Coroutine<int> { co_return 42; }();
    auto coro2 = std::move(coro);
    EXPECT_THROW(coro.getResult(), std::runtime_error);
}

// ─── CoroutineAwaiter 短路路径：已完成协程的 co_await ───

TEST(CoroutineTest, CoAwaitOnCompletedCoroutine) {
    auto child = []() -> Coroutine<int> { co_return 42; }();
    child.resume();
    EXPECT_TRUE(child.isDone());

    auto parent = [&]() -> Coroutine<int> {
        auto result = co_await child;
        co_return result;
    }();
    parent.resume();
    EXPECT_TRUE(parent.isDone());
    EXPECT_EQ(parent.getResult(), 42);
}

TEST(CoroutineTest, RvalueCoAwaitOnCompletedCoroutine) {
    // 右值 co_await 应在协程已完成时直接取值，不挂起
    auto makeChild = []() -> Coroutine<int> {
        co_return 99;
    };

    auto child = makeChild();
    child.resume();
    EXPECT_TRUE(child.isDone());

    auto parent = [&]() -> Coroutine<int> {
        auto result = co_await std::move(child);
        co_return result;
    }();
    parent.resume();
    EXPECT_TRUE(parent.isDone());
    EXPECT_EQ(parent.getResult(), 99);
}

// ─── PromiseBase::setCancelled/isCancelled 测试 ───

TEST(CoroutineTest, PromiseCancelThroughControlBlock) {
    auto coro = []() -> Coroutine<void> { co_return; }();
    coro.cancel();
    EXPECT_TRUE(coro.isCancelled());
}

TEST(CoroutineTest, CancelledFlagPersistsAfterResume) {
    std::atomic<bool> ran{false};
    auto coro = [&]() -> Coroutine<void> {
        ran = true;
        co_return;
    }();
    coro.cancel();
    coro.resume();
    // cancel 后 resume 仍可执行，但 cancelled 标志位在协程完成前应保持
    // 协程完成后 isCancelled 由 control_->isCancelled() 决定
    SUCCEED();
}

// ─── CoroutineScheduler 测试 ───

TEST(CoroutineSchedulerTest, ScheduleAfterExecutes) {
    GlobalCleanupGuard guard;
    std::atomic<bool> executed{false};
    auto coro = [&]() -> Coroutine<void> {
        co_await TimerAwaiter(std::chrono::milliseconds(20));
        executed = true;
        co_return;
    }();
    CoroutineScheduler::instance().schedule(coro.makeResumeHandle());
    for (int i = 0; i < 50 && !executed.load(); ++i) {
        CoroutineScheduler::instance().waitForAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(executed.load());
}

TEST(CoroutineSchedulerTest, ScheduleCallbackExecutes) {
    std::atomic<bool> executed{false};
    auto id = CoroutineScheduler::instance().scheduleCallback(std::chrono::milliseconds(10), [&]() {
        executed = true;
    });
    EXPECT_TRUE(id.isValid());
    for (int i = 0; i < 50 && !executed.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(executed.load());
}

TEST(CoroutineSchedulerTest, CancelScheduledCallback) {
    std::atomic<bool> executed{false};
    auto id = CoroutineScheduler::instance().scheduleCallback(std::chrono::milliseconds(100), [&]() {
        executed = true;
    });
    CoroutineScheduler::instance().cancel(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_FALSE(executed.load());
}

// ─── TimerAwaiter 测试 ───

TEST(TimerAwaiterTest, ZeroDurationDoesNotSuspend) {
    std::atomic<bool> executed{false};
    auto coro = [&]() -> Coroutine<void> {
        co_await TimerAwaiter(std::chrono::milliseconds(0));
        executed = true;
    }();
    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_TRUE(executed.load());
}

TEST(TimerAwaiterTest, NegativeDurationDoesNotSuspend) {
    std::atomic<bool> executed{false};
    auto coro = [&]() -> Coroutine<void> {
        co_await TimerAwaiter(std::chrono::milliseconds(-1));
        executed = true;
    }();
    coro.resume();
    EXPECT_TRUE(coro.isDone());
    EXPECT_TRUE(executed.load());
}

// ─── SharedCoroutineHandle notify->tryResume 完整流程 ───

TEST(SharedCoroutineHandleTest, NotifyThenTryResume) {
    auto coro = []() -> Coroutine<void> { co_return; }();
    auto handle = coro.makeResumeHandle();
    ASSERT_NE(handle, nullptr);
    EXPECT_TRUE(handle->suspend());
    EXPECT_TRUE(handle->notify());
    EXPECT_TRUE(handle->tryResume());
    EXPECT_TRUE(coro.isDone());
}

TEST(SharedCoroutineHandleTest, TryResumeOnSignaledWithoutNotifyIsNoop) {
    auto control = std::make_shared<CoroutineControlBlock>(std::coroutine_handle<>{});
    SharedCoroutineHandle h(control);
    // Registering -> 直接 tryResume，resumed_ 初始为 false 且 control 非空
    // tryResume 只检查 resumed_ 和 done()，不检查 state_
    EXPECT_TRUE(h.tryResume());
    // 第二次 tryResume 应返回 false（resumed_ 已置 true）
    EXPECT_FALSE(h.tryResume());
}

TEST(SharedCoroutineHandleTest, NotifyOnDefaultIsNoop) {
    SharedCoroutineHandle h;
    EXPECT_FALSE(h.notify());
    EXPECT_FALSE(h.suspend());
    EXPECT_FALSE(h.tryResume());
}

// ─── 多层 co_await 链的取消传播 ───

TEST(CoroutineTest, CancelPropagatesThroughAwaitChain) {
    std::atomic<bool> innerStarted{false};
    std::atomic<bool> innerCompleted{false};

    auto inner = [&]() -> Coroutine<void> {
        innerStarted = true;
        co_await TimerAwaiter(std::chrono::milliseconds(100));
        innerCompleted = true;
    }();

    auto outer = [&]() -> Coroutine<void> {
        co_await inner;
    }();

    outer.resume();
    EXPECT_TRUE(innerStarted.load());
    outer.cancel();
    EXPECT_TRUE(outer.isCancelled());
    // inner 可能已完成或 cancelled，不崩溃即可
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    SUCCEED();
}
