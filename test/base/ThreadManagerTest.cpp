//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "ThreadManager.h"

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "LoopThread.h"
#include "TaskThread.h"
#include "Util.h"

using namespace slark;
using namespace std::chrono_literals;

namespace {

ThreadManager::Config fastConfig(std::chrono::milliseconds threshold) {
    return {.pollInterval = 10ms, .blockThreshold = Time::TimeDelta::fromMilliSeconds(threshold)};
}

}  // namespace

TEST(ThreadManagerTest, ThreadsRegisterOnCreationAndUnregisterOnExit) {
    auto& manager = ThreadManager::shareInstance();
    auto taskThread = TaskThread::create(Util::genRandomName("managed-task"));
    auto loopThread =
        LoopThread::create(Util::genRandomName("managed-loop"), [](LoopThread& thread) {
            while (!thread.isStopRequested()) {
                thread.waitForWork([] {
                    return false;
                });
            }
        });
    ASSERT_NE(taskThread, nullptr);
    ASSERT_NE(loopThread, nullptr);

    EXPECT_FALSE(manager.add(taskThread));
    EXPECT_FALSE(manager.add(loopThread));

    taskThread->stop();
    taskThread->join();
    loopThread->stop();
    loopThread->join();

    EXPECT_FALSE(manager.remove(taskThread));
    EXPECT_FALSE(manager.remove(loopThread));
}

TEST(ThreadManagerTest, AddAndRemoveReportWhetherTheRegistryChanged) {
    ThreadManager manager;
    auto thread = TaskThread::create(Util::genRandomName("add-remove"));
    ASSERT_NE(thread, nullptr);

    EXPECT_TRUE(manager.add(thread));
    EXPECT_FALSE(manager.add(thread));
    EXPECT_EQ(manager.size(), 1u);
    EXPECT_TRUE(manager.remove(thread));
    EXPECT_FALSE(manager.remove(thread));
    EXPECT_EQ(manager.size(), 0u);
}

TEST(ThreadManagerTest, AddAfterStopJoinRemoveLeavesNoResidue) {
    ThreadManager manager;
    for (int round = 0; round < 1000; ++round) {
        auto thread = TaskThread::create(Util::genRandomName("m1"));
        ASSERT_NE(thread, nullptr);
        manager.add(thread);
        thread->stop();
        thread->join();
        manager.remove(thread);
        EXPECT_EQ(manager.size(), 0u) << "round " << round;
    }
}

TEST(ThreadManagerTest, AddIgnoresExitedThread) {
    ThreadManager manager;
    auto thread = TaskThread::create(Util::genRandomName("exited"));
    ASSERT_NE(thread, nullptr);
    thread->stop();
    thread->join();
    EXPECT_FALSE(thread->isRunning());
    EXPECT_FALSE(manager.add(thread));
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_FALSE(manager.remove(thread));
}

TEST(ThreadManagerTest, AddIgnoresNullptr) {
    ThreadManager manager;
    EXPECT_FALSE(manager.add(nullptr));
    EXPECT_FALSE(manager.remove(nullptr));
}

TEST(ThreadManagerTest, SizeCountsOnlyThreadsThatStillExist) {
    ThreadManager manager;
    auto kept = TaskThread::create(Util::genRandomName("kept"));
    auto dropped = TaskThread::create(Util::genRandomName("dropped"));
    ASSERT_NE(kept, nullptr);
    ASSERT_NE(dropped, nullptr);
    manager.add(kept);
    manager.add(dropped);
    EXPECT_EQ(manager.size(), 2u);

    dropped->stop();
    dropped->join();
    dropped.reset();
    EXPECT_EQ(manager.size(), 1u);

    EXPECT_TRUE(manager.remove(kept));
    EXPECT_EQ(manager.size(), 0u);
    kept->stop();
    kept->join();
}

TEST(ThreadManagerTest, BlockedThreadIsReportedWithItsName) {
    ThreadManager manager(fastConfig(50ms));
    std::mutex mutex;
    std::condition_variable reported;
    std::vector<std::string> names;
    manager.setBlockCallback([&](std::string_view name, Time::TimeDelta duration) {
        EXPECT_GE(duration, Time::TimeDelta::fromMilliSeconds(50ms));
        {
            std::lock_guard lock(mutex);
            names.emplace_back(name);
        }
        reported.notify_all();
    });

    auto thread = TaskThread::create(Util::genRandomName("blocked"));
    ASSERT_NE(thread, nullptr);
    manager.add(thread);
    std::promise<void> release;
    auto released = release.get_future();
    ASSERT_TRUE(thread->postTask(ThreadTask([&] {
                                     released.wait();
                                 }))
                    .has_value());

    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(reported.wait_for(lock, 5s, [&] {
            return !names.empty();
        }));
        EXPECT_EQ(names.front(), thread->getName());
    }
    release.set_value();
    thread->stop();
    thread->join();
}

TEST(ThreadManagerTest, BlockedThreadIsLoggedEvenWithoutACallback) {
    ThreadManager manager(fastConfig(50ms));
    auto thread = TaskThread::create(Util::genRandomName("logged"));
    ASSERT_NE(thread, nullptr);
    manager.add(thread);
    std::promise<void> release;
    auto released = release.get_future();
    ASSERT_TRUE(thread->postTask(ThreadTask([&] {
                                     released.wait();
                                 }))
                    .has_value());

    testing::internal::CaptureStdout();
    std::this_thread::sleep_for(300ms);
    const auto console = testing::internal::GetCapturedStdout();

    release.set_value();
    thread->stop();
    thread->join();
    EXPECT_NE(console.find("[" + thread->getName() + "] is blocking"), std::string::npos);
}

TEST(ThreadManagerTest, IdleThreadIsNeverReported) {
    ThreadManager manager(fastConfig(200ms));
    std::atomic<int> reports{0};
    manager.setBlockCallback([&](std::string_view, Time::TimeDelta) {
        ++reports;
    });

    auto thread = TaskThread::create(Util::genRandomName("idle"));
    ASSERT_NE(thread, nullptr);
    manager.add(thread);
    ASSERT_TRUE(thread->postAndWait([] {}).has_value());

    testing::internal::CaptureStdout();
    std::this_thread::sleep_for(800ms);
    const auto console = testing::internal::GetCapturedStdout();

    EXPECT_EQ(reports.load(), 0);
    EXPECT_EQ(console.find("ThreadManager report"), std::string::npos);
    thread->stop();
    thread->join();
}

TEST(ThreadManagerTest, MonitorStartsAndStopsCleanlyUnderChurn) {
    for (int round = 0; round < 200; ++round) {
        ThreadManager manager;
        if (round % 2 == 1) {
            auto thread = TaskThread::create(Util::genRandomName("churn"));
            ASSERT_NE(thread, nullptr);
            manager.add(thread);
            thread->stop();
            thread->join();
        }
    }
}
