#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

#include "TaskThread.h"

using namespace slark;

TEST(ThreadFuzzTest, ConcurrentPostAndStop) {
    auto thread = TaskThread::create("FuzzTest");
    std::atomic<int> taskCount{0};
    std::atomic<bool> running{true};

    std::thread poster([&]() {
        std::mt19937 rng(42);
        while (running.load()) {
            (void)thread->postTask([&taskCount]() {
                taskCount++;
                std::this_thread::yield();
            });
            std::this_thread::yield();
            if (rng() % 10 == 0) {
                (void)thread->postTask(std::chrono::milliseconds(1 + (rng() % 10)), [&taskCount]() {
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
        auto thread = TaskThread::create("CycleTest" + std::to_string(i));
        (void)thread->postTask([]() {
            std::this_thread::yield();
        });
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        thread->stop();
        thread->join();
    }
    SUCCEED();
}

TEST(ThreadFuzzTest, PostAfterStopNoCrash) {
    auto thread = TaskThread::create("PostAfterStop");
    thread->stop();
    thread->join();
    EXPECT_NO_THROW({
        auto result = thread->postTask([]() {
        });
        EXPECT_FALSE(result.has_value());
    });
    SUCCEED();
}

TEST(ThreadFuzzTest, RepeatTaskCancellation) {
    auto thread = TaskThread::create("RepeatTest");
    std::atomic<int> count{0};

    std::vector<TimerId> ids;
    for (int i = 0; i < 20; ++i) {
        auto id = thread->postRepeatTask(std::chrono::milliseconds(1), [&count]() {
            count++;
        });
        ids.push_back(id);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    for (size_t i = 0; i < ids.size() / 2; ++i) {
        (void)thread->cancel(ids[i]);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    thread->stop();
    thread->join();
    SUCCEED();
}
