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
        ASSERT_GT(time, 5 * 1000);
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

// pause 期间投递的任务不执行，start 后才执行
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
    ASSERT_EQ(counter.load(), 0); // pause 期间不执行

    thread->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1); // start 后执行
}

// pause 期间 repeatTask 不执行
TEST(Thread, pauseBlocksRepeatTask) {
    std::atomic<int32_t> counter{0};
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->scheduleRepeat(std::chrono::milliseconds(10), [&counter]() {
        counter.fetch_add(1);
    });
    thread->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    auto countBeforePause = counter.load();
    ASSERT_GE(countBeforePause, 1);

    thread->pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), countBeforePause); // pause 期间不增加
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
        order.push_back(2);
    });

    thread->schedule([&]() {
        std::lock_guard lock(orderMutex);
        order.push_back(1);
    });
    thread->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    thread->stop();

    std::lock_guard lock(orderMutex);
    ASSERT_GE(order.size(), 2u);
    ASSERT_EQ(order[0], 1);
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
    ASSERT_LE(counter.load(), countBeforeCancel + 1);
}

TEST(Thread, repeatTaskNoDanglingPointer) {
    using namespace std::chrono;
    std::atomic<int32_t> counter{0};
    std::atomic<bool> stopFlag{false};

    auto thread = Thread::create(Util::genRandomName("thread"));
    std::vector<TimerId> timerIds;

    for (int i = 0; i < 10; ++i) {
        timerIds.push_back(thread->scheduleRepeat(5ms, [&counter, &stopFlag]() {
            if (!stopFlag.load()) {
                counter.fetch_add(1);
                std::this_thread::sleep_for(1ms);
            }
        }));
    }
    thread->start();

    std::this_thread::sleep_for(30ms);

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
    SUCCEED();
}

// pause 期间投递大量任务不会触发唤醒，start 后全部执行
TEST(Thread, pauseDoesNotWakeOnSchedule) {
    using namespace std::chrono;
    constexpr int32_t kTaskCount = 20;
    std::atomic<int32_t> counter{0};
    std::latch allDone{kTaskCount};

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();
    std::this_thread::sleep_for(10ms);
    thread->pause();
    std::this_thread::sleep_for(10ms);

    for (int32_t i = 0; i < kTaskCount; ++i) {
        thread->schedule([&counter, &allDone]() {
            counter.fetch_add(1);
            allDone.count_down();
        });
    }

    // 等待足够长时间，确认 pause 期间任务不执行
    std::this_thread::sleep_for(80ms);
    ASSERT_EQ(counter.load(), 0);

    thread->start();
    allDone.wait();
    ASSERT_EQ(counter.load(), kTaskCount);
}

// pause 期间 repeatTask 暂停，start 后继续执行
TEST(Thread, repeatTaskResumesAfterStart) {
    using namespace std::chrono;
    std::atomic<int32_t> counter{0};

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->scheduleRepeat(10ms, [&counter]() {
        counter.fetch_add(1);
    });
    thread->start();
    std::this_thread::sleep_for(35ms);

    thread->pause();
    auto countAtPause = counter.load();
    ASSERT_GE(countAtPause, 1);

    std::this_thread::sleep_for(50ms);
    ASSERT_EQ(counter.load(), countAtPause); // pause 期间不增加

    thread->start();
    std::this_thread::sleep_for(35ms);
    ASSERT_GT(counter.load(), countAtPause); // start 后继续增加
}

// cancelSchedule 取消一次性延迟任务
TEST(Thread, cancelDelayedTask) {
    using namespace std::chrono;
    std::atomic<bool> executed{false};

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();

    auto id = thread->schedule(100ms, [&executed]() {
        executed.store(true);
    });
    ASSERT_TRUE(id.isValid());

    std::this_thread::sleep_for(20ms);
    thread->cancelSchedule(id);

    std::this_thread::sleep_for(200ms);
    ASSERT_FALSE(executed.load());
}

// stop 后调用 start 无效，线程保持退出状态
TEST(Thread, startAfterStopIsNoop) {
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();
    thread->stop();
    ASSERT_TRUE(thread->isExit());

    thread->start();
    ASSERT_FALSE(thread->isRunning());
    ASSERT_TRUE(thread->isExit());
}

// setThreadName 不影响线程正常执行
TEST(Thread, setThreadName) {
    std::atomic<int32_t> counter{0};
    auto thread = Thread::create("initial-name");
    thread->start();

    thread->setThreadName("renamed-thread");
    ASSERT_EQ(thread->getName(), "renamed-thread");

    thread->schedule([&counter]() {
        counter.fetch_add(1);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1);
}