//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "LoopThread.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>

#include "Channel.hpp"
#include "ThreadTestSupport.h"
#include "Util.h"

using namespace slark;
using namespace std::chrono_literals;

TEST(LoopThreadTest, PostTaskWakesWaitAndWakeHook) {
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::atomic<int> hookCalls{0};
    std::atomic<int> taskCalls{0};

    auto worker = LoopThread::create(
        Util::genRandomName("loop"),
        [&entered](LoopThread& thread) {
            entered.set_value();
            while (!thread.isStopRequested()) {
                thread.drainPendingWork();
                thread.waitForWork([] { return false; });
            }
        },
        [&hookCalls] { ++hookCalls; });
    ASSERT_NE(worker, nullptr);
    ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);

    ASSERT_TRUE(worker->postAndWait([&] {
        EXPECT_TRUE(worker->isCurrentThread());
        ++taskCalls;
    }).has_value());
    EXPECT_EQ(taskCalls.load(), 1);
    EXPECT_GT(hookCalls.load(), 0);

    worker->stop();
    worker->join();
    EXPECT_TRUE(worker->hasExited());
}

TEST(LoopThreadTest, NotifyReleasesPredicateWait) {
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::atomic<bool> ready{false};
    std::atomic<int> completed{0};

    auto worker = LoopThread::create(Util::genRandomName("loop"), [&](LoopThread& thread) {
        entered.set_value();
        thread.waitForWork([&] { return ready.load(std::memory_order_acquire); });
        if (!thread.isStopRequested()) {
            ++completed;
        }
    });
    ASSERT_NE(worker, nullptr);
    ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);

    ready.store(true, std::memory_order_release);
    worker->notify();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!worker->hasExited() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_EQ(completed.load(), 1);
    EXPECT_TRUE(worker->hasExited());
    worker->join();
}

TEST(LoopThreadTest, StopWakesWaitForWork) {
    auto worker = LoopThread::create(Util::genRandomName("loop"), [](LoopThread& thread) {
        while (!thread.isStopRequested()) {
            thread.waitForWork([] { return false; });
        }
    });
    ASSERT_NE(worker, nullptr);

    worker->stop();
    worker->join();
    EXPECT_TRUE(worker->hasExited());
}

TEST(LoopThreadTest, StopHookObservesStopRequested) {
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::promise<bool> hookSawStop;
    auto hookFuture = hookSawStop.get_future();
    Thread* threadPtr = nullptr;

    auto worker = LoopThread::create(
        Util::genRandomName("loop"),
        [&](LoopThread& thread) {
            entered.set_value();
            while (!thread.isStopRequested()) {
                thread.drainPendingWork();
                thread.waitForWork([] { return false; });
            }
        },
        [&] { hookSawStop.set_value(threadPtr && threadPtr->isStopRequested()); });
    ASSERT_NE(worker, nullptr);
    threadPtr = worker.get();
    ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);

    worker->stop();
    ASSERT_EQ(hookFuture.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(hookFuture.get());
    worker->join();
}

TEST(LoopThreadTest, WakeHookCanPostTaskWithoutDeadlock) {
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::atomic<int> hookPosted{0};
    std::atomic<bool> hookArmed{false};
    Thread* threadPtr = nullptr;

    auto worker = LoopThread::create(
        Util::genRandomName("loop"),
        [&](LoopThread& thread) {
            entered.set_value();
            while (!thread.isStopRequested()) {
                thread.drainPendingWork();
                thread.waitForWork([] { return false; });
            }
        },
        [&] {
            // The hook runs outside the internal lock, so posting from it is safe.
            // Post only once: a hook that always posts would recurse via its own postTask.
            if (threadPtr && !hookArmed.exchange(true)) {
                (void)threadPtr->postTask([&hookPosted] { ++hookPosted; });
            }
        });
    ASSERT_NE(worker, nullptr);
    threadPtr = worker.get();
    ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);

    ASSERT_TRUE(worker->postTask([] {}).has_value());
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (hookPosted.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_GT(hookPosted.load(), 0);
    worker->stop();
    worker->join();
}

TEST(LoopThreadTest, TaskReachableThroughHookWhileBlockedInChannelReceive) {
    auto [sender, receiver] = Channel<int>::create();
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::promise<void> processed;
    auto processedFuture = processed.get_future();

    auto worker = LoopThread::create(
        Util::genRandomName("loop"),
        [&](LoopThread& thread) {
            entered.set_value();
            while (!thread.isStopRequested()) {
                thread.drainPendingWork();
                auto event = receiver->receive();
                if (!event.has_value()) {
                    if (event.error() == ChannelEventType::Awakened) {
                        continue;
                    }
                    break;
                }
                processed.set_value();
            }
        },
        [rx = receiver.get()] {
            if (rx) {
                rx->wakeup();
            }
        });
    ASSERT_NE(worker, nullptr);
    ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);
    std::this_thread::sleep_for(20ms);  // let the loop block inside receive()

    // No data is sent: only the hook-driven wakeup lets the queued task run.
    // The loop drains the task first, then receive() returns Awakened and blocks again.
    std::promise<void> taskRan;
    auto taskRanFuture = taskRan.get_future();
    ASSERT_TRUE(worker->postTask([&] { taskRan.set_value(); }).has_value());
    EXPECT_EQ(taskRanFuture.wait_for(2s), std::future_status::ready);

    worker->stop();
    const auto joinDone = std::async(std::launch::async, [&] { worker->join(); });
    EXPECT_EQ(joinDone.wait_for(2s), std::future_status::ready);
}

TEST(LoopThreadTest, StopJoinRepeatedWhileBlockedInReceive) {
    for (int round = 0; round < 200; ++round) {
        auto [sender, receiver] = Channel<int>::create();
        std::promise<void> entered;
        auto enteredFuture = entered.get_future();

        auto worker = LoopThread::create(
            Util::genRandomName("loop"),
            [&](LoopThread& thread) {
                entered.set_value();
                while (!thread.isStopRequested()) {
                    thread.drainPendingWork();
                    auto event = receiver->receive();
                    if (!event.has_value() && event.error() != ChannelEventType::Awakened) {
                        break;
                    }
                }
            },
            [rx = receiver.get()] {
                if (rx) {
                    rx->wakeup();
                }
            });
        ASSERT_NE(worker, nullptr);
        ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);

        worker->stop();
        auto joined = std::async(std::launch::async, [&] { worker->join(); });
        ASSERT_EQ(joined.wait_for(1s), std::future_status::ready) << "round " << round;
        EXPECT_TRUE(worker->hasExited());
    }
}

TEST(LoopThreadTest, NoLostWakeupUnderPressure) {
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::atomic<bool> flag{false};
    std::atomic<int> wakeups{0};

    auto worker = LoopThread::create(Util::genRandomName("loop"), [&](LoopThread& thread) {
        entered.set_value();
        while (!thread.isStopRequested()) {
            thread.waitForWork([&] { return flag.load(std::memory_order_acquire); });
            if (thread.isStopRequested()) {
                break;
            }
            flag.store(false, std::memory_order_release);
            ++wakeups;
        }
    });
    ASSERT_NE(worker, nullptr);
    ASSERT_EQ(enteredFuture.wait_for(2s), std::future_status::ready);

    // Per-round bounded wait: every notify must produce an observed wakeup, which is
    // the lost-wakeup property. Coalescing across rapid signals is allowed, so the
    // total count is not asserted.
    constexpr int kRounds = 100000;
    for (int i = 0; i < kRounds; ++i) {
        const auto before = wakeups.load();
        flag.store(true, std::memory_order_release);
        worker->notify();
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (wakeups.load() == before && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        ASSERT_GT(wakeups.load(), before) << "round " << i;
    }
    worker->stop();
    worker->join();
}
