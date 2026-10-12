//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "ThreadManager.h"

#include <gtest/gtest.h>

#include "LoopThread.h"
#include "TaskThread.h"
#include "Util.h"

using namespace slark;

TEST(ThreadManagerTest, RegistersBothThreadTypes) {
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

    manager.add(taskThread);
    manager.add(loopThread);
    manager.add(taskThread);  // duplicate add is idempotent
    manager.remove(nullptr);  // unregistered thread is a no-op

    taskThread->stop();
    taskThread->join();
    loopThread->stop();
    loopThread->join();
    manager.remove(taskThread);
    manager.remove(loopThread);
    manager.remove(taskThread);  // double remove is a no-op
}

TEST(ThreadManagerTest, AddAfterStopJoinRemoveLeavesNoResidue) {
    auto& manager = ThreadManager::shareInstance();
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
    auto& manager = ThreadManager::shareInstance();
    auto thread = TaskThread::create(Util::genRandomName("exited"));
    ASSERT_NE(thread, nullptr);
    thread->stop();
    thread->join();
    EXPECT_FALSE(thread->isRunning());
    manager.add(thread);  // exited threads are ignored by design
    manager.remove(thread);
}

TEST(ThreadManagerTest, AddIgnoresNullptr) {
    ThreadManager::shareInstance().add(nullptr);
    ThreadManager::shareInstance().remove(nullptr);
}
