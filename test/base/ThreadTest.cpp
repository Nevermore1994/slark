//
// Copyright (c) 2026 Nevermore-. All rights reserved.
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
}

// pause 期间投递的任务不执行，start 后才执行
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
    ASSERT_EQ(counter.load(), 0); // pause 期间不执行

    thread->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1); // start 后执行
}

// pause 期间 repeatTask 不执行
TEST(Thread, pauseBlocksRepeatTask) {
    std::atomic<int32_t> counter{0};
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->postRepeatTask(std::chrono::milliseconds(10), [&counter]() {
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
    ASSERT_EQ(order[0], 1);
}

TEST(Thread, postRepeatTask) {
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
    thread->cancelTask(id);

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
            thread->cancelTask(id);
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
    thread->postRepeatTask(10ms, [&counter]() {
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

    thread->postTask([&counter]() {
        counter.fetch_add(1);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1);
}

// custom loop 模式下 shouldStop/pause/resume 正常工作
TEST(Thread, createWithLoopShouldStopPauseResume) {
    using namespace std::chrono;
    std::atomic<int> state{0}; // 0=init, 1=running, 2=paused, 3=resumed, 4=stopped
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

// custom loop 的 drainPendingWork 能消费 postTask
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

// 没有 postTask 时 drainPendingWork 是空操作
TEST(Thread, createWithLoopDrainPendingWorkNoop) {
    using namespace std::chrono;
    std::atomic<bool> loopExited{false};

    auto thread = Thread::createWithLoop("testNoop", [&](Thread& t) {
        while (!t.shouldStop()) {
            t.drainPendingWork(); // 空队列，应该快速返回
            loopExited.store(true);
            break; // 只跑一轮
        }
    });

    std::this_thread::sleep_for(30ms);
    ASSERT_TRUE(loopExited.load());
    ASSERT_TRUE(thread->isExit()); // loop 已退出，thread 已 stop
}

// postTask 在 shouldStop() 返回 true 后被拒绝
TEST(Thread, createWithLoopPostTaskAfterStop) {
    auto thread = Thread::createWithLoop("testPostStop", [&](Thread& t) {
        while (!t.shouldStop()) {
            std::this_thread::sleep_for(1ms);
        }
    });

    thread->stop();
    std::this_thread::sleep_for(30ms);
    ASSERT_TRUE(thread->isExit());

    auto result = thread->postTask([]() {});
    ASSERT_FALSE(result.has_value());
}

// create 模式（无 custom loop）行为不变
TEST(Thread, createModeStillWorks) {
    std::atomic<int> counter{0};
    auto thread = Thread::create(Util::genRandomName("thread"));
    thread->start();

    thread->postTask([&]() { counter.fetch_add(1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 1);

    thread->pause();
    ASSERT_FALSE(thread->isRunning());

    thread->start();
    ASSERT_TRUE(thread->isRunning());

    thread->postTask([&]() { counter.fetch_add(1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(counter.load(), 2);

    thread->stop();
    ASSERT_TRUE(thread->isExit());
}