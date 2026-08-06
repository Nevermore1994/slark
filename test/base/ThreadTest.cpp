//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Thread.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <latch>
#include <thread>
#include <vector>

#include "Util.h"

using namespace slark;

TEST(Thread, state) {
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->postRepeatTask(std::chrono::milliseconds(1), []() {
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

TEST(Thread, postTask) {
    std::atomic<int32_t> counter{0};
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();

    auto result = thread->postTask([&counter]() {
        counter.fetch_add(1);
    });
    ASSERT_TRUE(result.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1);
    thread->stop();
}

TEST(Thread, postTaskWhenPaused) {
    std::atomic<int32_t> counter{0};
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    thread->pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto result = thread->postTask([&counter]() {
        counter.fetch_add(1);
    });
    ASSERT_TRUE(result.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 0);

    thread->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1);
    thread->stop();
}

TEST(Thread, pauseBlocksRepeatTask) {
    std::atomic<int32_t> counter{0};
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->postRepeatTask(std::chrono::milliseconds(10), [&counter]() {
        counter.fetch_add(1);
    });
    thread->start();

    while (counter.load() < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    thread->pause();
    auto countBeforePause = counter.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), countBeforePause);
    thread->stop();
}

TEST(Thread, postTaskAfterStop) {
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();
    thread->stop();

    auto result = thread->postTask([]() {
    });
    ASSERT_FALSE(result.has_value());
}

TEST(Thread, postTaskMultiProducer) {
    constexpr int32_t kThreads = 4;
    constexpr int32_t kTasksPerThread = 100;
    std::atomic<int32_t> counter{0};

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();

    std::vector<std::jthread> producers;
    for (int32_t t = 0; t < kThreads; ++t) {
        producers.emplace_back([&]() {
            for (int32_t i = 0; i < kTasksPerThread; ++i) {
                thread->postTask([&counter]() {
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
    thread->stop();
}

TEST(Thread, postRepeatTask) {
    std::vector<int32_t> order;
    std::mutex orderMutex;

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->postRepeatTask(std::chrono::milliseconds(50), [&]() {
        std::lock_guard lock(orderMutex);
        order.push_back(2);
    });

    thread->postTask([&]() {
        std::lock_guard lock(orderMutex);
        order.push_back(1);
    });
    thread->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    thread->stop();

    std::lock_guard lock(orderMutex);
    ASSERT_GE(order.size(), 2u);
    EXPECT_TRUE(std::ranges::find(order, 1) != order.end());
    EXPECT_TRUE(std::ranges::find(order, 2) != order.end());
}

TEST(Thread, postRepeatTaskCount) {
    using namespace std::chrono;
    std::atomic<int32_t> counter{0};

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->postRepeatTask(10ms, [&counter]() {
        counter.fetch_add(1);
    });
    thread->start();

    std::this_thread::sleep_for(55ms);
    thread->stop();

    ASSERT_GE(counter.load(), 3);
    ASSERT_LE(counter.load(), 8);
}

TEST(Thread, cancelTask) {
    using namespace std::chrono;
    std::atomic<int32_t> counter{0};

    auto thread = Thread::create(Util::genRandomName("thread"));
    auto id = thread->postRepeatTask(10ms, [&counter]() {
        counter.fetch_add(1);
    });
    thread->start();

    std::this_thread::sleep_for(35ms);
    auto countBeforeCancel = counter.load();
    thread->cancelRepeatTask(id);

    std::this_thread::sleep_for(50ms);
    ASSERT_LE(counter.load(), countBeforeCancel + 1);
    thread->stop();
}

TEST(Thread, repeatTaskNoDanglingPointer) {
    using namespace std::chrono;
    std::atomic<int32_t> counter{0};
    std::atomic<bool> stopFlag{false};

    auto thread = Thread::create(Util::genRandomName("thread"));
    std::vector<TimerId> timerIds;

    for (int i = 0; i < 10; ++i) {
        timerIds.push_back(thread->postRepeatTask(5ms, [&counter, &stopFlag]() {
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
            thread->cancelRepeatTask(id);
        });
    }
    for (auto& t : cancelThreads) {
        t.join();
    }

    stopFlag.store(true);
    std::this_thread::sleep_for(20ms);
    thread->stop();
    SUCCEED();
}

TEST(Thread, pauseDoesNotWakeOnPostTask) {
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
        thread->postTask([&counter, &allDone]() {
            counter.fetch_add(1);
            allDone.count_down();
        });
    }

    std::this_thread::sleep_for(80ms);
    ASSERT_EQ(counter.load(), 0);

    thread->start();
    allDone.wait();
    ASSERT_EQ(counter.load(), kTaskCount);
    thread->stop();
}

TEST(Thread, repeatTaskResumesAfterStart) {
    using namespace std::chrono;
    std::atomic<int32_t> counter{0};

    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->postRepeatTask(10ms, [&counter]() {
        counter.fetch_add(1);
    });
    thread->start();
    std::this_thread::sleep_for(35ms);

    thread->pause();
    auto countAtPause = counter.load();
    ASSERT_GE(countAtPause, 1);

    std::this_thread::sleep_for(50ms);
    ASSERT_EQ(counter.load(), countAtPause);

    thread->start();
    std::this_thread::sleep_for(35ms);
    ASSERT_GT(counter.load(), countAtPause);
    thread->stop();
}

TEST(Thread, startAfterStopIsNoop) {
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();
    thread->stop();
    ASSERT_TRUE(thread->isExit());

    thread->start();
    ASSERT_FALSE(thread->isRunning());
    ASSERT_TRUE(thread->isExit());
}

TEST(Thread, setThreadName) {
    std::atomic<int32_t> counter{0};
    auto thread = Thread::create("initial-name");
    thread->start();

    thread->setThreadName("renamed-thread");
    ASSERT_EQ(thread->getName(), "renamed-thread");

    thread->postTask([&counter]() {
        counter.fetch_add(1);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1);
    thread->stop();
}

TEST(Thread, createWithLoopShouldStopPauseResume) {
    using namespace std::chrono;
    std::atomic<int> state{0};  // 0=init, 1=running, 2=paused, 3=resumed, 4=stopped
    std::latch loopEntered{1};

    auto thread = Thread::createWithLoop("testLoop", [&](Thread& t) {
        state.store(1);
        loopEntered.count_down();
        while (!t.shouldStop()) {
            std::this_thread::sleep_for(1ms);
        }
        state.store(4);
    });

    loopEntered.wait();
    ASSERT_EQ(state.load(), 1);
    ASSERT_FALSE(thread->isExit());

    thread->pause();
    std::this_thread::sleep_for(20ms);
    ASSERT_FALSE(thread->isRunning());
    ASSERT_FALSE(thread->isExit());

    thread->start();
    std::this_thread::sleep_for(20ms);
    ASSERT_TRUE(thread->isRunning());

    thread->stop();
    std::this_thread::sleep_for(30ms);
    ASSERT_TRUE(thread->isExit());
    ASSERT_EQ(state.load(), 4);
}

TEST(Thread, createWithLoopDrainPendingWork) {
    using namespace std::chrono;
    std::atomic<int> counter{0};
    std::vector<int> order;
    std::mutex orderMutex;
    std::latch taskProcessed{1};

    auto thread = Thread::createWithLoop("testDrain", [&](Thread& t) {
        while (!t.shouldStop()) {
            t.drainPendingWork();
            std::this_thread::sleep_for(1ms);
        }
    });

    thread->postTask([&]() {
        counter.fetch_add(1);
        order.push_back(1);
        taskProcessed.count_down();
    });

    taskProcessed.wait();
    ASSERT_EQ(counter.load(), 1);
    ASSERT_EQ(order.size(), 1u);
    ASSERT_EQ(order[0], 1);

    thread->stop();
    std::this_thread::sleep_for(30ms);
    ASSERT_TRUE(thread->isExit());
}

TEST(Thread, createWithLoopDrainPendingWorkNoop) {
    using namespace std::chrono;
    std::atomic<bool> loopExited{false};

    auto thread = Thread::createWithLoop("testNoop", [&](Thread& t) {
        while (!t.shouldStop()) {
            t.drainPendingWork();
            loopExited.store(true);
            break;
        }
    });

    std::this_thread::sleep_for(30ms);
    ASSERT_TRUE(loopExited.load());
    ASSERT_TRUE(thread->isExit());
}

TEST(Thread, createWithLoopPostTaskAfterStop) {
    using namespace std::chrono;
    auto thread = Thread::createWithLoop("testPostStop", [&](Thread& t) {
        while (!t.shouldStop()) {
            std::this_thread::sleep_for(1ms);
        }
    });

    thread->stop();
    std::this_thread::sleep_for(30ms);
    ASSERT_TRUE(thread->isExit());

    auto result = thread->postTask([]() {
    });
    ASSERT_FALSE(result.has_value());
}

TEST(Thread, createModeStillWorks) {
    std::atomic<int> counter{0};
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();

    thread->postTask([&]() {
        counter.fetch_add(1);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1);

    thread->pause();
    ASSERT_FALSE(thread->isRunning());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    thread->start();
    ASSERT_TRUE(thread->isRunning());

    std::latch latch(1);
    thread->postTask([&]() {
        latch.count_down();
    });
    latch.wait();
    ASSERT_EQ(counter.load(), 1);

    std::latch secondTaskLatch(1);
    thread->postTask([&]() {
        counter.fetch_add(1);
        secondTaskLatch.count_down();
    });
    secondTaskLatch.wait();
    ASSERT_EQ(counter.load(), 2);

    thread->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ASSERT_TRUE(thread->isExit());
}