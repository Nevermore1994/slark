//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

#include "TaskThread.h"
#include "ThreadTestSupport.h"
#include "Util.h"

using namespace slark;
using namespace std::chrono_literals;

TEST(ThreadBaseTest, PostAndWaitRunsOnWorkerAndPublishesResult) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);

    bool onWorker = false;
    std::thread::id workerId;
    int value = 0;
    auto result = worker->postAndWait([&] {
        onWorker = worker->isCurrentThread();
        workerId = worker->getId();
        value = 42;
    });

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(onWorker);
    EXPECT_EQ(workerId, worker->getId());
    EXPECT_EQ(value, 42);
    EXPECT_NE(worker->serial(), 0u);

    worker->stop();
    worker->join();
    EXPECT_TRUE(worker->hasExited());
}

TEST(ThreadBaseTest, StopIsIdempotentAndConcurrentStopIsSafe) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);

    constexpr int kStoppers = 8;
    std::latch ready{kStoppers};
    std::vector<std::thread> stoppers;
    for (int i = 0; i < kStoppers; ++i) {
        stoppers.emplace_back([&] {
            ready.count_down();
            ready.wait();
            worker->stop();
        });
    }
    for (auto& stopper : stoppers) {
        stopper.join();
    }
    worker->join();
    EXPECT_TRUE(worker->hasExited());
    EXPECT_TRUE(worker->isStopRequested());
}

TEST(ThreadBaseTest, JoinFromWorkerThreadReturnsImmediately) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);

    auto done = worker->postAndWait([&] { worker->join(); });
    EXPECT_TRUE(done.has_value());
    worker->stop();
    worker->join();
}

TEST(ThreadBaseTest, GetIdStableAcrossJoin) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);
    ASSERT_TRUE(worker->postAndWait([] {}).has_value());
    const auto idBefore = worker->getId();
    EXPECT_NE(idBefore, std::this_thread::get_id());

    worker->stop();
    worker->join();
    EXPECT_EQ(worker->getId(), idBefore);
    EXPECT_FALSE(worker->isCurrentThread());
}

TEST(ThreadBaseTest, StopRejectsFurtherTasks) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);
    worker->stop();
    worker->join();

    std::atomic<int> calls{0};
    auto result = worker->postTask([&] {
        ++calls;
    });
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(calls.load(), 0);
}

TEST(ThreadBaseTest, StopDropsQueuedTasksAndReleasesCaptures) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);

    test::BlockingTask blocker;
    auto entered = blocker.takeEnteredFuture();
    ASSERT_TRUE(worker->postTask([blocker] { blocker(); }).has_value());
    ASSERT_EQ(entered.wait_for(2s), std::future_status::ready);

    auto token = test::TrackedToken::create();
    auto capture = token->get();
    std::atomic<int> executed{0};
    for (int i = 0; i < 8; ++i) {
        auto captureCopy = token->get();
        ASSERT_TRUE(worker
                        ->postTask([captureCopy, &executed] {
                            ++executed;
                        })
                        .has_value());
    }
    EXPECT_EQ(capture.use_count(), 10);  // token + capture + 8 queued copies

    worker->stop();  // the loop must exit without draining the queued batch
    blocker.release();
    worker->join();
    EXPECT_EQ(executed.load(), 0);
    EXPECT_EQ(capture.use_count(), 2);  // token + capture survive, queue copies destroyed
}

TEST(ThreadBaseTest, ConcurrentJoinIsSafe) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);
    worker->stop();

    std::thread first([&] {
        worker->join();
    });
    std::thread second([&] {
        worker->join();
    });
    first.join();
    second.join();
    EXPECT_TRUE(worker->hasExited());
}

TEST(ThreadBaseTest, ConcurrentJoinWhileThreadRunsThenStops) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);

    std::atomic<bool> stopIssued{false};
    std::thread joiner([&] {
        while (!stopIssued.load()) {
            worker->join();
            std::this_thread::yield();
        }
    });
    std::this_thread::sleep_for(20ms);
    stopIssued.store(true);
    worker->stop();
    joiner.join();
    worker->join();
    EXPECT_TRUE(worker->hasExited());
}

TEST(ThreadBaseTest, PostAndWaitCancelledWhenWorkerStopsWithTaskQueued) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);

    test::BlockingTask blocker;
    auto entered = blocker.takeEnteredFuture();
    ASSERT_TRUE(worker->postTask([blocker] { blocker(); }).has_value());
    ASSERT_EQ(entered.wait_for(2s), std::future_status::ready);

    auto outcome = test::withWatchdog([&] {
        worker->stop();  // queued completion task must be dropped with Cancelled
        return worker->postAndWait([] {});
    });
    EXPECT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error(), Error::Cancelled);
    blocker.release();
    worker->join();
}

TEST(ThreadBaseTest, ConcurrentPostAndWaitWhileStopping) {
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);

    constexpr int kCallers = 16;
    std::latch go{kCallers};
    std::vector<std::thread> callers;
    std::atomic<int> succeeded{0};
    std::atomic<int> cancelled{0};
    for (int i = 0; i < kCallers; ++i) {
        callers.emplace_back([&] {
            go.count_down();
            go.wait();
            auto result = worker->postAndWait([] {});
            if (result.has_value()) {
                ++succeeded;
            } else {
                EXPECT_EQ(result.error(), Error::Cancelled);
                ++cancelled;
            }
        });
    }
    std::thread stopper([&] {
        worker->stop();
    });
    for (auto& caller : callers) {
        caller.join();
    }
    stopper.join();
    worker->join();
    EXPECT_EQ(succeeded.load() + cancelled.load(), kCallers);
}

TEST(ThreadBaseTest, SerialsAreUniqueAcrossThreads) {
    constexpr int kCount = 32;
    std::vector<uint64_t> serials;
    for (int i = 0; i < kCount; ++i) {
        auto worker = TaskThread::create(Util::genRandomName("serial"));
        ASSERT_NE(worker, nullptr);
        serials.push_back(worker->serial());
        worker->stop();
        worker->join();
    }
    std::sort(serials.begin(), serials.end());
    EXPECT_EQ(std::adjacent_find(serials.begin(), serials.end()), serials.end());
}

TEST(ThreadBaseTest, LastReferenceReleasedInTaskDoesNotHang) {
    test::withWatchdog([] {
        auto worker = TaskThread::create(Util::genRandomName("self-release"));
        ASSERT_NE(worker, nullptr);
        // Move the last external reference into the task; the worker's entry holds
        // its own strong reference until the loop returns, so ~Thread runs there.
        worker->postTask([holder = worker] {});
        worker.reset();
    });
    SUCCEED();
}

TEST(ThreadBaseTest, LastReferenceReleasedWhileTaskRunningBlocksUntilBatchDone) {
    test::withWatchdog([] {
        auto worker = TaskThread::create(Util::genRandomName("ext-release"));
        ASSERT_NE(worker, nullptr);

        test::BlockingTask blocker;
        auto entered = blocker.takeEnteredFuture();
        ASSERT_TRUE(worker->postTask([blocker] { blocker(); }).has_value());
        ASSERT_TRUE(worker->postTask([] {}).has_value());  // second task in same batch

        std::thread releaser([&] { worker.reset(); });
        std::this_thread::sleep_for(50ms);
        blocker.release();
        releaser.join();
    });
    SUCCEED();
}

TEST(ThreadBaseTest, ProducersPreserveTheirOwnTaskOrder) {
    constexpr int kProducerCount = 4;
    constexpr int kTasksPerProducer = 100;
    auto worker = TaskThread::create(Util::genRandomName("worker"));
    ASSERT_NE(worker, nullptr);

    std::mutex mutex;
    std::vector<std::vector<int>> observed(static_cast<size_t>(kProducerCount));
    std::vector<std::thread> producers;
    for (int producer = 0; producer < kProducerCount; ++producer) {
        producers.emplace_back([&, producer] {
            for (int sequence = 0; sequence < kTasksPerProducer; ++sequence) {
                auto result = worker->postTask([&, producer, sequence] {
                    std::lock_guard lock(mutex);
                    observed[static_cast<size_t>(producer)].push_back(sequence);
                });
                EXPECT_TRUE(result.has_value());
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    ASSERT_TRUE(worker->postAndWait([] {}).has_value());

    for (const auto& sequence : observed) {
        ASSERT_EQ(sequence.size(), kTasksPerProducer);
        EXPECT_TRUE(std::is_sorted(sequence.begin(), sequence.end()));
    }
    worker->stop();
    worker->join();
}
