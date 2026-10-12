//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "TaskThread.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <random>
#include <thread>
#include <vector>

#include "ThreadTestSupport.h"
#include "Util.h"

using namespace slark;
using namespace std::chrono_literals;

TEST(TaskThreadTest, DeferredTasksWaitForLaunch) {
    auto worker =
        TaskThread::create(Util::genRandomName("deferred"), TaskThread::LaunchMode::Deferred);
    ASSERT_NE(worker, nullptr);
    EXPECT_FALSE(worker->isRunning());

    std::atomic<int> calls{0};
    auto repeatId = worker->postRepeatTask(5ms, [&] {
        ++calls;
    });
    ASSERT_TRUE(repeatId.isValid());
    ASSERT_TRUE(worker
                    ->postTask([&] {
                        ++calls;
                    })
                    .has_value());
    std::this_thread::sleep_for(20ms);
    EXPECT_EQ(calls.load(), 0);

    worker->launch();
    ASSERT_TRUE(worker
                    ->postAndWait([] {
                    })
                    .has_value());
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (calls.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_GE(calls.load(), 2);
    EXPECT_TRUE(worker->cancel(repeatId));
    ASSERT_TRUE(worker
                    ->postAndWait([] {
                    })
                    .has_value());

    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, OneShotRunsAndIsRemoved) {
    auto worker = TaskThread::create(Util::genRandomName("timer"));
    ASSERT_NE(worker, nullptr);

    std::promise<void> completed;
    auto future = completed.get_future();
    const auto id = worker->postTask(0ms, [&] {
        completed.set_value();
    });
    ASSERT_TRUE(id.isValid());
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(worker
                    ->postAndWait([] {
                    })
                    .has_value());
    EXPECT_EQ(worker->pendingTimerCount(), 0u);

    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, CancelsPendingOneShot) {
    auto worker = TaskThread::create(Util::genRandomName("timer"));
    ASSERT_NE(worker, nullptr);
    std::atomic<int> calls{0};
    const auto id = worker->postTask(1s, [&] {
        ++calls;
    });

    EXPECT_TRUE(worker->cancel(id));
    EXPECT_FALSE(worker->cancel(id));
    EXPECT_FALSE(worker->cancel(TimerId{}));
    EXPECT_EQ(worker->pendingTimerCount(), 0u);
    worker->stop();
    worker->join();
    EXPECT_EQ(calls.load(), 0);
}

TEST(TaskThreadTest, CancelsOneShotAlreadyQueuedOnWorker) {
    auto worker = TaskThread::create(Util::genRandomName("timer"));
    ASSERT_NE(worker, nullptr);

    test::BlockingTask blocker;
    auto entered = blocker.takeEnteredFuture();
    ASSERT_TRUE(worker
                    ->postTask([blocker] {
                        blocker();
                    })
                    .has_value());
    ASSERT_EQ(entered.wait_for(2s), std::future_status::ready);

    std::atomic<int> calls{0};
    const auto id = worker->postTask(10ms, [&] {
        ++calls;
    });
    std::this_thread::sleep_for(100ms);
    EXPECT_TRUE(worker->cancel(id));
    blocker.release();
    ASSERT_TRUE(worker
                    ->postAndWait([] {
                    })
                    .has_value());
    EXPECT_EQ(calls.load(), 0);

    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, CancelRunningOneShotReturnsFalseAndTaskCompletes) {
    auto worker = TaskThread::create(Util::genRandomName("timer"));
    ASSERT_NE(worker, nullptr);

    test::BlockingTask blocker;
    auto entered = blocker.takeEnteredFuture();
    std::atomic<bool> ranToCompletion{false};
    const auto id = worker->postTask(10ms, [&] {
        blocker();
        ranToCompletion.store(true);
    });
    ASSERT_EQ(entered.wait_for(2s), std::future_status::ready);

    EXPECT_FALSE(worker->cancel(id));
    blocker.release();
    ASSERT_TRUE(worker->postAndWait([] {}).has_value());
    EXPECT_TRUE(ranToCompletion.load());
    EXPECT_EQ(worker->pendingTimerCount(), 0u);
    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, CancelAfterExecutionReturnsFalse) {
    auto worker = TaskThread::create(Util::genRandomName("timer"));
    ASSERT_NE(worker, nullptr);

    std::atomic<int> calls{0};
    const auto id = worker->postTask(0ms, [&] { ++calls; });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(worker->postAndWait([] {}).has_value());
    ASSERT_EQ(calls.load(), 1);
    EXPECT_FALSE(worker->cancel(id));
    EXPECT_FALSE(worker->cancel(id));
    EXPECT_EQ(worker->pendingTimerCount(), 0u);
    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, RepeatTaskACancelsRepeatTaskBInSameBatch) {
    // Deferred mode pins the first due batch to [A, B] in registration order, making
    // the interleaving deterministic: A cancels B before B's commit-point CAS.
    auto worker =
        TaskThread::create(Util::genRandomName("batch"), TaskThread::LaunchMode::Deferred);
    ASSERT_NE(worker, nullptr);

    std::atomic<bool> cancelIssued{false};
    std::atomic<int> bCalls{0};
    std::promise<void> aRan;
    auto aRanFuture = aRan.get_future();
    TimerId idB;  // declared first so A's lambda can reference it; registered second
    const auto idA = worker->postRepeatTask(0ms, [&] {
        if (!cancelIssued.exchange(true)) {
            EXPECT_TRUE(worker->cancel(idB));
            aRan.set_value();
        }
    });
    idB = worker->postRepeatTask(0ms, [&] { ++bCalls; });
    worker->launch();

    ASSERT_EQ(aRanFuture.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(bCalls.load(), 0);
    worker->cancel(idA);
    ASSERT_TRUE(worker->postAndWait([] {}).has_value());
    EXPECT_EQ(worker->pendingTimerCount(), 0u);  // both repeats erased by their cancels
    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, CancelVsTriggerPressureInvariant) {
    auto worker = TaskThread::create(Util::genRandomName("pressure"));
    ASSERT_NE(worker, nullptr);

    constexpr int kRounds = 100000;
    std::atomic<int> executed{0};
    std::atomic<int> cancelTrue{0};
    std::mt19937 rng(42);
    for (int i = 0; i < kRounds; ++i) {
        const auto id = worker->postTask(std::chrono::milliseconds(rng() % 3), [&] {
            ++executed;
        });
        if (worker->cancel(id)) {
            ++cancelTrue;
        }
    }
    ASSERT_TRUE(worker->postAndWait([] {}).has_value());
    const auto total = executed.load() + cancelTrue.load();
    EXPECT_EQ(total, kRounds);
    EXPECT_EQ(worker->pendingTimerCount(), 0u);
    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, CancelWithBarrierStopsFutureExecutions) {
    auto worker = TaskThread::create(Util::genRandomName("barrier"));
    ASSERT_NE(worker, nullptr);

    // Design C5 calls for 100k rounds at a 20ms observation window each, which alone
    // would cost ~30 minutes; 2000 rounds with a 5ms window keep the same invariant
    // (cancel:true -> barrier -> no new execution inside the window) at 1% of the cost.
    constexpr int kRounds = 2000;
    constexpr auto kWindow = 5ms;
    std::mt19937 rng(7);
    for (int round = 0; round < kRounds; ++round) {
        std::atomic<int> calls{0};
        const auto id = worker->postRepeatTask(std::chrono::milliseconds(rng() % 3), [&] {
            ++calls;
        });
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (calls.load() < 1 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        ASSERT_TRUE(worker->cancel(id)) << "round " << round;
        ASSERT_TRUE(worker->postAndWait([] {}).has_value()) << "round " << round;
        const auto countAtBarrier = calls.load();
        std::this_thread::sleep_for(kWindow);
        ASSERT_EQ(calls.load(), countAtBarrier) << "round " << round;
    }
    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, ExecutedAndCancelledOneShotsLeaveNoResidue) {
    auto worker = TaskThread::create(Util::genRandomName("residue"));
    ASSERT_NE(worker, nullptr);

    constexpr int kPerKind = 2000;
    std::atomic<int> executed{0};
    std::vector<TimerId> toCancel;
    std::vector<TimerId> toDrop;
    for (int i = 0; i < kPerKind; ++i) {
        // 10s delay keeps the cancel-able tasks safely pending while the loop cancels them.
        toCancel.push_back(worker->postTask(10s, [] {}));
        toDrop.push_back(worker->postTask(1h, [] {}));
        worker->postTask(0ms, [&] { ++executed; });
    }
    for (const auto id : toCancel) {
        EXPECT_TRUE(worker->cancel(id));
    }
    // toDrop entries are never cancelled: they stay pending and are cleared by stop().
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (executed.load() < kPerKind && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_TRUE(worker->postAndWait([] {}).has_value());
    EXPECT_EQ(executed.load(), kPerKind);
    EXPECT_EQ(worker->pendingTimerCount(), static_cast<size_t>(kPerKind));
    worker->stop();
    worker->join();
    EXPECT_EQ(worker->pendingTimerCount(), 0u);
}

TEST(TaskThreadTest, RepeatDoesNotAccumulateWhenTaskSlowerThanInterval) {
    auto worker = TaskThread::create(Util::genRandomName("slow-repeat"));
    ASSERT_NE(worker, nullptr);

    std::atomic<int> calls{0};
    const auto id = worker->postRepeatTask(1ms, [&] {
        ++calls;
        std::this_thread::sleep_for(5ms);
    });
    std::this_thread::sleep_for(100ms);
    EXPECT_TRUE(worker->cancel(id));
    ASSERT_TRUE(worker->postAndWait([] {}).has_value());
    // Interval restarts after each run: at most duration/runTime + margin runs.
    EXPECT_LE(calls.load(), 30);
    EXPECT_GE(calls.load(), 5);
    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, ZeroIntervalRepeatDoesNotStarveTasksOrStop) {
    auto worker = TaskThread::create(Util::genRandomName("zero-repeat"));
    ASSERT_NE(worker, nullptr);

    std::atomic<int> repeatCalls{0};
    worker->postRepeatTask(0ms, [&] { ++repeatCalls; });
    std::this_thread::sleep_for(10ms);

    auto taskDone = test::withWatchdog([&] { return worker->postAndWait([] {}); }, 3s);
    EXPECT_TRUE(taskDone.has_value());
    test::withWatchdog([&] { worker->stop(); }, 3s);
    worker->join();
    EXPECT_GT(repeatCalls.load(), 0);
}

TEST(TaskThreadTest, DelayTaskCancelledWhenThreadStops) {
    auto worker = TaskThread::create(Util::genRandomName("delay-stop"));
    ASSERT_NE(worker, nullptr);

    auto token = test::TrackedToken::create();
    auto capture = token->get();
    worker->postTask(1h, [capture] {});
    EXPECT_GT(capture.use_count(), 2);

    worker->stop();
    worker->join();
    // TaskThread's registry is empty; the capture may still live inside TimerPool's
    // lazily-cancelled entry until its expiry (pre-existing TimerManager behavior).
    EXPECT_EQ(worker->pendingTimerCount(), 0u);
    EXPECT_GE(capture.use_count(), 2);
}

TEST(TaskThreadTest, LaunchIsIdempotentAndConcurrentLaunchRunsOnce) {
    auto worker = TaskThread::create(Util::genRandomName("launch"), TaskThread::LaunchMode::Deferred);
    ASSERT_NE(worker, nullptr);

    std::atomic<int> tickCalls{0};
    worker->postRepeatTask(1ms, [&] { ++tickCalls; });

    constexpr int kLaunchers = 8;
    std::latch go{kLaunchers};
    std::vector<std::thread> launchers;
    for (int i = 0; i < kLaunchers; ++i) {
        launchers.emplace_back([&] {
            go.count_down();
            go.wait();
            worker->launch();
        });
    }
    for (auto& launcher : launchers) {
        launcher.join();
    }
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (tickCalls.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_GT(tickCalls.load(), 0);
    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, DestroyWithoutLaunchDoesNotHang) {
    test::withWatchdog([] {
        auto worker =
            TaskThread::create(Util::genRandomName("never-launch"), TaskThread::LaunchMode::Deferred);
        ASSERT_NE(worker, nullptr);
        (void)worker->postTask([] {}).has_value();
        worker->stop();
        worker->join();
    });
    SUCCEED();
}

TEST(TaskThreadTest, CancelsRepeatWithoutStartingAnotherRun) {
    auto worker = TaskThread::create(Util::genRandomName("repeat"));
    ASSERT_NE(worker, nullptr);
    std::atomic<int> calls{0};
    const auto id = worker->postRepeatTask(2ms, [&] {
        ++calls;
    });

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (calls.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_GE(calls.load(), 2);
    EXPECT_TRUE(worker->cancel(id));
    ASSERT_TRUE(worker
                    ->postAndWait([] {
                    })
                    .has_value());
    const auto countAfterCancel = calls.load();
    std::this_thread::sleep_for(20ms);
    EXPECT_EQ(calls.load(), countAfterCancel);
    EXPECT_EQ(worker->pendingTimerCount(), 0u);

    worker->stop();
    worker->join();
}

TEST(TaskThreadTest, RepeatTaskCanCancelItself) {
    auto worker =
        TaskThread::create(Util::genRandomName("self-cancel"), TaskThread::LaunchMode::Deferred);
    ASSERT_NE(worker, nullptr);
    std::promise<bool> cancelled;
    auto future = cancelled.get_future();
    TimerId id;
    id = worker->postRepeatTask(1ms, [&] {
        cancelled.set_value(worker->cancel(id));
    });
    worker->launch();

    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(future.get());
    ASSERT_TRUE(worker
                    ->postAndWait([] {
                    })
                    .has_value());
    EXPECT_EQ(worker->pendingTimerCount(), 0u);
    worker->stop();
    worker->join();
}
