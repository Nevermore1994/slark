//
// Created by Nevermore on 2024/4/2.
// slark ThreadTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include "Thread.h"

#include "Util.h"

#include <atomic>
#include <format>
#include <gtest/gtest.h>
#include <latch>
#include <thread>
#include <vector>

using namespace slark;

TEST(Thread, state) {
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->scheduleRepeat(std::chrono::milliseconds(1), []() {
    });
    thread->start();
    ASSERT_EQ(thread->isRunning(), true);
    ASSERT_EQ(thread->isExit(), false);
    thread->pause();
    ASSERT_EQ(thread->isRunning(), false);
    ASSERT_EQ(thread->isExit(), false);
    thread->start();
    ASSERT_EQ(thread->isRunning(), true);
    ASSERT_EQ(thread->isExit(), false);
    thread->pause();
    ASSERT_EQ(thread->isRunning(), false);
    ASSERT_EQ(thread->isExit(), false);
    thread->pause();
    ASSERT_EQ(thread->isRunning(), false);
    ASSERT_EQ(thread->isExit(), false);
    thread->stop();
    ASSERT_EQ(thread->isRunning(), false);
    ASSERT_EQ(thread->isExit(), true);
}

TEST(Thread, timer) {
    using namespace std::chrono;
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();
    auto t = Time::nowTimeStamp();
    thread->schedule(10ms, [&t] {
        auto time = Time::nowTimeStamp() - t;
        ASSERT_GT(time, 5 * 1000); // 5ms tolerance
        std::println("delay us: {}", time.point());
    });
    std::this_thread::sleep_for(50ms);
}

TEST(Thread, schedule) {
    std::atomic<int32_t> counter{0};
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();

    auto result = thread->schedule([&counter]() {
        counter.fetch_add(1);
    });
    ASSERT_TRUE(result.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1);
}

TEST(Thread, scheduleWhenPaused) {
    std::atomic<int32_t> counter{0};
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    thread->pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto result = thread->schedule([&counter]() {
        counter.fetch_add(1);
    });
    ASSERT_TRUE(result.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1);
}

TEST(Thread, scheduleAfterStop) {
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();
    thread->stop();

    auto result = thread->schedule([]() {
    });
    ASSERT_FALSE(result.has_value());
}

TEST(Thread, scheduleMultiProducer) {
    constexpr int32_t kThreads = 4;
    constexpr int32_t kTasksPerThread = 100;
    std::atomic<int32_t> counter{0};

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();

    std::vector<std::jthread> producers;
    for (int32_t t = 0; t < kThreads; ++t) {
        producers.emplace_back([&]() {
            for (int32_t i = 0; i < kTasksPerThread; ++i) {
                thread->schedule([&counter]() {
                    counter.fetch_add(1);
                });
            }
        });
    }
    for (auto& p : producers) {
        p.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_EQ(counter.load(), kThreads * kTasksPerThread);
}

TEST(Thread, scheduleDelayed) {
    using namespace std::chrono;
    std::atomic<bool> executed{false};

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();

    auto timerId = thread->schedule(50ms, [&]() {
        executed.store(true);
    });
    ASSERT_TRUE(timerId.isValid());

    std::this_thread::sleep_for(20ms);
    ASSERT_FALSE(executed.load());

    std::this_thread::sleep_for(300ms);
    ASSERT_TRUE(executed.load());
}

TEST(Thread, schedulePriorityOverRepeat) {
    std::vector<int32_t> order;
    std::mutex orderMutex;

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->scheduleRepeat(std::chrono::milliseconds(50), [&]() {
        std::lock_guard lock(orderMutex);
        order.push_back(2); // repeat task
    });

    thread->schedule([&]() {
        std::lock_guard lock(orderMutex);
        order.push_back(1); // schedule task
    });
    thread->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    thread->stop();

    std::lock_guard lock(orderMutex);
    ASSERT_GE(order.size(), 2u);
    ASSERT_EQ(order[0], 1); // schedule task runs first
}

TEST(Thread, scheduleRepeat) {
    using namespace std::chrono;
    std::atomic<int32_t> counter{0};

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->scheduleRepeat(10ms, [&counter]() {
        counter.fetch_add(1);
    });
    thread->start();

    std::this_thread::sleep_for(55ms);
    thread->stop();

    // ~10ms interval, 55ms total => should execute ~5 times
    ASSERT_GE(counter.load(), 3);
    ASSERT_LE(counter.load(), 8);
}

TEST(Thread, cancelSchedule) {
    using namespace std::chrono;
    std::atomic<int32_t> counter{0};

    auto thread = Thread::create(Util::genRandomName("thread"));
    auto id = thread->scheduleRepeat(10ms, [&counter]() {
        counter.fetch_add(1);
    });
    thread->start();

    std::this_thread::sleep_for(35ms);
    auto countBeforeCancel = counter.load();
    thread->cancelSchedule(id);

    std::this_thread::sleep_for(50ms);
    // counter should not increase after cancel
    ASSERT_LE(counter.load(), countBeforeCancel + 1);
}

TEST(Thread, repeatTaskNoDanglingPointer) {
    // 测试并发取消不会导致悬空指针
    using namespace std::chrono;
    std::atomic<int32_t> counter{0};
    std::atomic<bool> stopFlag{false};

    auto thread = Thread::create(Util::genRandomName("thread"));
    std::vector<TimerId> timerIds;

    for (int i = 0; i < 10; ++i) {
        timerIds.push_back(thread->scheduleRepeat(5ms, [&counter, &stopFlag]() {
            if (!stopFlag.load()) {
                counter.fetch_add(1);
                std::this_thread::sleep_for(1ms); // 模拟耗时任务
            }
        }));
    }
    thread->start();

    std::this_thread::sleep_for(30ms);

    // 并发取消所有定时器
    std::vector<std::jthread> cancelThreads;
    for (auto id : timerIds) {
        cancelThreads.emplace_back([thread, id]() {
            thread->cancelSchedule(id);
        });
    }
    for (auto& t : cancelThreads) {
        t.join();
    }

    stopFlag.store(true);
    std::this_thread::sleep_for(20ms);
    // 测试不应崩溃
    SUCCEED();
}