//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Scheduler.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "ManualScheduler.h"

using namespace std::chrono_literals;
using slark::ThreadScheduler;
using slark::test::ManualScheduler;

namespace {

class Latch {
 public:
    void signal() {
        {
            std::lock_guard lock(mutex_);
            ++count_;
        }
        cond_.notify_all();
    }

    bool waitFor(int n, std::chrono::milliseconds timeout = 3000ms) {
        std::unique_lock lock(mutex_);
        return cond_.wait_for(lock, timeout, [&] {
            return count_ >= n;
        });
    }

    int count() {
        std::lock_guard lock(mutex_);
        return count_;
    }

 private:
    std::mutex mutex_;
    std::condition_variable cond_;
    int count_ = 0;
};

}  // namespace

// ManualScheduler

class ManualSchedulerTest : public ::testing::Test {
 protected:
    ManualScheduler sched;
    std::vector<int> order;
};

TEST_F(ManualSchedulerTest, PostRunsOnlyWhenDriven) {
    sched.execute([&] {
        order.push_back(1);
    });
    EXPECT_TRUE(order.empty());
    sched.runUntilIdle();
    EXPECT_EQ(order, (std::vector<int>{1}));
}

TEST_F(ManualSchedulerTest, PostIsFifo) {
    for (int i = 0; i < 5; ++i) {
        sched.execute([&, i] {
            order.push_back(i);
        });
    }
    sched.runUntilIdle();
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST_F(ManualSchedulerTest, TimerFiresOnlyAfterItsTime) {
    sched.postAt(1, sched.now() + 100ms, [&] {
        order.push_back(1);
    });
    sched.advance(99ms);
    EXPECT_TRUE(order.empty());
    sched.advance(1ms);
    EXPECT_EQ(order, (std::vector<int>{1}));
}

TEST_F(ManualSchedulerTest, TimersFireInTimeOrderNotInsertionOrder) {
    sched.postAt(1, sched.now() + 300ms, [&] {
        order.push_back(3);
    });
    sched.postAt(2, sched.now() + 100ms, [&] {
        order.push_back(1);
    });
    sched.postAt(3, sched.now() + 200ms, [&] {
        order.push_back(2);
    });
    sched.advance(1s);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST_F(ManualSchedulerTest, TimersAtTheSameInstantKeepInsertionOrder) {
    const auto at = sched.now() + 50ms;
    sched.postAt(1, at, [&] {
        order.push_back(1);
    });
    sched.postAt(2, at, [&] {
        order.push_back(2);
    });
    sched.advance(50ms);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST_F(ManualSchedulerTest, TaskObservesTheTimeItWasScheduledFor) {
    const auto start = sched.now();
    uint64_t seenAt = 0;
    sched.postAt(1, start + 250ms, [&] {
        seenAt = sched.now().point();
    });
    sched.advance(1s);
    EXPECT_EQ(seenAt, (start + 250ms).point());
    EXPECT_EQ(sched.now().point(), (start + 1s).point());
}

TEST_F(ManualSchedulerTest, SameSlotReplacesThePendingTimer) {
    sched.postAt(7, sched.now() + 100ms, [&] {
        order.push_back(100);
    });
    sched.postAt(7, sched.now() + 200ms, [&] {
        order.push_back(200);
    });
    EXPECT_EQ(sched.pendingTimers(), 1u);
    sched.advance(1s);
    EXPECT_EQ(order, (std::vector<int>{200}));
}

TEST_F(ManualSchedulerTest, DifferentSlotsAreIndependent) {
    sched.postAt(1, sched.now() + 100ms, [&] {
        order.push_back(1);
    });
    sched.postAt(2, sched.now() + 100ms, [&] {
        order.push_back(2);
    });
    EXPECT_EQ(sched.pendingTimers(), 2u);
    sched.advance(100ms);
    EXPECT_EQ(order.size(), 2u);
}

TEST_F(ManualSchedulerTest, CancelRemovesTheTimer) {
    sched.postAt(1, sched.now() + 100ms, [&] {
        order.push_back(1);
    });
    sched.cancel(1);
    sched.cancel(1);
    sched.cancel(99);
    sched.advance(1s);
    EXPECT_TRUE(order.empty());
    EXPECT_EQ(sched.pendingTimers(), 0u);
}

TEST_F(ManualSchedulerTest, TimerInThePastRunsWithoutAdvancing) {
    sched.advance(1s);
    sched.postAt(1, slark::Time::TimePoint(sched.now().point() - 500'000), [&] {
        order.push_back(1);
    });
    sched.runUntilIdle();
    EXPECT_EQ(order, (std::vector<int>{1}));
}

TEST_F(ManualSchedulerTest, TaskPostedByATaskRunsWithinTheSameAdvance) {
    sched.postAt(1, sched.now() + 100ms, [&] {
        order.push_back(1);
        sched.execute([&] {
            order.push_back(2);
        });
        sched.postAt(2, sched.now() + 50ms, [&] {
            order.push_back(3);
        });
    });
    sched.advance(150ms);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST_F(ManualSchedulerTest, TaskCanReplaceItsOwnSlot) {
    int fired = 0;
    sched.postAt(1, sched.now() + 10ms, [&] {
        ++fired;
        if (fired < 3) {
            sched.postAt(1, sched.now() + 10ms, [&] {
                ++fired;
            });
        }
    });
    sched.advance(1s);
    EXPECT_EQ(fired, 2);
}

TEST_F(ManualSchedulerTest, ADueTimerRunsAfterQueuedTasks) {
    sched.postAt(1, sched.now(), [&] {
        order.push_back(2);
    });
    sched.execute([&] {
        order.push_back(1);
    });
    sched.runUntilIdle();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST_F(ManualSchedulerTest, OnSchedulerThreadIsTrueOnTheOwner) {
    EXPECT_TRUE(sched.onSchedulerThread());
    bool other = true;
    std::thread([&] {
        other = sched.onSchedulerThread();
    }).join();
    EXPECT_FALSE(other);
}

// ThreadScheduler (real thread)

TEST(ThreadSchedulerTest, PostRunsOnTheSchedulerThread) {
    ThreadScheduler sched("schedulerTest");
    ASSERT_TRUE(sched.valid());
    EXPECT_FALSE(sched.onSchedulerThread());

    Latch latch;
    std::atomic<bool> onThread{false};
    std::atomic<std::thread::id> runId{};
    sched.execute([&] {
        onThread = sched.onSchedulerThread();
        runId = std::this_thread::get_id();
        latch.signal();
    });
    ASSERT_TRUE(latch.waitFor(1));
    EXPECT_TRUE(onThread.load());
    EXPECT_NE(runId.load(), std::this_thread::get_id());
}

TEST(ThreadSchedulerTest, PostPreservesOrder) {
    ThreadScheduler sched("schedulerTest");
    Latch latch;
    std::vector<int> order;
    std::mutex m;
    for (int i = 0; i < 50; ++i) {
        sched.execute([&, i] {
            {
                std::lock_guard lock(m);
                order.push_back(i);
            }
            latch.signal();
        });
    }
    ASSERT_TRUE(latch.waitFor(50));
    std::lock_guard lock(m);
    ASSERT_EQ(order.size(), 50u);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(order[static_cast<size_t>(i)], i);
    }
}

TEST(ThreadSchedulerTest, PostAtFiresAfterTheDelay) {
    ThreadScheduler sched("schedulerTest");
    Latch latch;
    const auto start = slark::Time::nowTimeStamp();
    std::atomic<uint64_t> firedAt{0};
    sched.postAt(1, start + 40ms, [&] {
        firedAt = slark::Time::nowTimeStamp().point();
        latch.signal();
    });
    ASSERT_TRUE(latch.waitFor(1));
    EXPECT_GE(firedAt.load(), (start + 35ms).point());
}

TEST(ThreadSchedulerTest, SameSlotReplacesSoTheFirstNeverFires) {
    ThreadScheduler sched("schedulerTest");
    Latch latch;
    std::atomic<int> firstFired{0};
    const auto now = slark::Time::nowTimeStamp();
    sched.postAt(5, now + 60ms, [&] {
        ++firstFired;
    });
    sched.postAt(5, now + 120ms, [&] {
        latch.signal();
    });
    ASSERT_TRUE(latch.waitFor(1));
    std::this_thread::sleep_for(60ms);
    EXPECT_EQ(firstFired.load(), 0);
    EXPECT_EQ(latch.count(), 1);
}

TEST(ThreadSchedulerTest, CancelPreventsTheTimer) {
    ThreadScheduler sched("schedulerTest");
    std::atomic<int> fired{0};
    sched.postAt(1, slark::Time::nowTimeStamp() + 50ms, [&] {
        ++fired;
    });
    sched.cancel(1);
    std::this_thread::sleep_for(150ms);
    EXPECT_EQ(fired.load(), 0);
}

TEST(ThreadSchedulerTest, StopIsIdempotentAndRefusesLaterWork) {
    ThreadScheduler sched("schedulerTest");
    sched.stop();
    sched.stop();
    std::atomic<int> ran{0};
    sched.execute([&] {
        ++ran;
    });
    sched.postAt(1, slark::Time::nowTimeStamp(), [&] {
        ++ran;
    });
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(ran.load(), 0);
}

TEST(ThreadSchedulerTest, StopRunsEveryAcceptedTaskBeforeReturning) {
    ThreadScheduler sched("schedulerTest");
    Latch started;
    sched.execute([&] {
        started.signal();
        std::this_thread::sleep_for(100ms);
    });
    ASSERT_TRUE(started.waitFor(1));

    std::atomic<int> ran{0};
    int accepted = 0;
    for (int i = 0; i < 10; ++i) {
        const bool ok = sched.tryExecute([&] {
            ++ran;
        });
        accepted += ok ? 1 : 0;
    }
    sched.stop();
    EXPECT_EQ(accepted, 10);
    EXPECT_EQ(ran.load(), accepted) << "every accepted task must run before stop() returns";
}

TEST(ThreadSchedulerTest, WorkPostedByADrainedTaskIsRefused) {
    ThreadScheduler sched("schedulerTest");
    Latch started;
    sched.execute([&] {
        started.signal();
        std::this_thread::sleep_for(100ms);
    });
    ASSERT_TRUE(started.waitFor(1));

    std::atomic<bool> drainedRan{false};
    std::atomic<bool> lateRefused{false};
    std::atomic<bool> lateRan{false};
    ASSERT_TRUE(sched.tryExecute([&] {
        drainedRan = true;
        lateRefused = !sched.tryExecute([&] {
            lateRan = true;
        });
    }));
    sched.stop();
    EXPECT_TRUE(drainedRan.load());
    EXPECT_TRUE(lateRefused.load());
    EXPECT_FALSE(lateRan.load());
}

TEST(ThreadSchedulerTest, StopCancelsTimerWhoseCallbackIsQueuedBehindWork) {
    ThreadScheduler sched("schedulerTest");
    Latch started;
    sched.execute([&] {
        started.signal();
        std::this_thread::sleep_for(100ms);
    });
    ASSERT_TRUE(started.waitFor(1));

    std::atomic<int> fired{0};
    sched.postAt(7, slark::Time::nowTimeStamp(), [&] {
        ++fired;
    });
    std::this_thread::sleep_for(30ms);
    sched.stop();
    EXPECT_EQ(fired.load(), 0) << "a timer must not start once the scheduler is stopping";
}

TEST(ThreadSchedulerTest, TryExecuteRefusesAfterStop) {
    ThreadScheduler sched("schedulerTest");
    Latch ran;
    EXPECT_TRUE(sched.tryExecute([&] {
        ran.signal();
    }));
    ASSERT_TRUE(ran.waitFor(1));

    sched.stop();
    std::atomic<int> refusedRan{0};
    EXPECT_FALSE(sched.tryExecute([&] {
        ++refusedRan;
    })) << "a stopped scheduler must refuse work so the caller can unwind";
    std::this_thread::sleep_for(20ms);
    EXPECT_EQ(refusedRan.load(), 0);
}

TEST(ThreadSchedulerTest, DestructorWithPendingWorkDoesNotHang) {
    std::atomic<int> ran{0};
    {
        ThreadScheduler sched("schedulerTest");
        sched.postAt(1, slark::Time::nowTimeStamp() + 10s, [&] {
            ++ran;
        });
        sched.execute([] {
            std::this_thread::sleep_for(20ms);
        });
    }
    EXPECT_EQ(ran.load(), 0);
}

TEST(ThreadSchedulerTest, StopFromInsideATaskDoesNotDeadlock) {
    auto sched = std::make_unique<ThreadScheduler>("schedulerTest");
    Latch latch;
    auto* raw = sched.get();
    raw->execute([raw, &latch] {
        raw->stop();
        latch.signal();
    });
    ASSERT_TRUE(latch.waitFor(1));
    sched.reset();
}
