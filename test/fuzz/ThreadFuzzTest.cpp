#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

#include "Thread.h"

using namespace slark;

TEST(ThreadFuzzTest, ConcurrentPostAndStop) {
    auto thread = Thread::create("FuzzTest");
    std::atomic<int> taskCount{0};
    std::atomic<bool> running{true};

    thread->start();

    std::thread poster([&]() {
        std::mt19937 rng(42);
        while (running.load()) {
            thread->postTask([&taskCount]() {
                taskCount++;
                // 模拟随机耗时
                std::this_thread::yield();
            });
            std::this_thread::yield();
            if (rng() % 10 == 0) {
                thread->postTask(std::chrono::milliseconds(1 + (rng() % 10)), [&taskCount]() {
                    taskCount++;
                });
            }
        }
    });

    std::thread stopper([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        thread->stop();
        thread->join();
        running.store(false);
    });

    poster.join();
    stopper.join();
    SUCCEED();
}

TEST(ThreadFuzzTest, StartStopMultipleTimes) {
    for (int i = 0; i < 30; ++i) {
        auto thread = Thread::create("CycleTest" + std::to_string(i));
        thread->start();
        thread->postTask([]() {
            std::this_thread::yield();
        });
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        thread->stop();
        thread->join();
    }
    SUCCEED();
}

TEST(ThreadFuzzTest, PostAfterStopNoCrash) {
    auto thread = Thread::create("PostAfterStop");
    thread->start();
    thread->stop();
    thread->join();
    // stop 后 postTask 不应崩溃
    EXPECT_NO_THROW({
        auto result = thread->postTask([]() {
        });
        (void)result;
    });
    SUCCEED();
}

TEST(ThreadFuzzTest, RepeatTaskCancellation) {
    auto thread = Thread::create("RepeatTest");
    thread->start();
    std::atomic<int> count{0};

    std::vector<TimerId> ids;
    for (int i = 0; i < 20; ++i) {
        auto id = thread->postRepeatTask(std::chrono::milliseconds(1), [&count]() {
            count++;
        });
        ids.push_back(id);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // 取消一半
    for (size_t i = 0; i < ids.size() / 2; ++i) {
        thread->cancelRepeatTask(ids[i]);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    thread->stop();
    thread->join();
    SUCCEED();
}
