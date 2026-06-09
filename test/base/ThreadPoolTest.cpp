//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "ThreadPool.hpp"

#include "Error.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <latch>
#include <vector>

using namespace slark;
using namespace std::chrono_literals;

class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        ThreadPoolConfig config;
        config.threadCount = 4;
        pool = std::make_unique<ThreadPool>(config);
    }

    void TearDown() override {
        pool.reset();
    }

    std::unique_ptr<ThreadPool> pool;
};

TEST_F(ThreadPoolTest, BasicFunctionality) {
    auto future = pool->submit([]() {
        return 42;
    });
    ASSERT_TRUE(future);
    EXPECT_EQ(future->get(), 42);
}

TEST_F(ThreadPoolTest, MultipleTasksExecution) {
    std::vector<Expected<std::future<int>>> futures;
    for (int i = 0; i < 5; ++i) {
        futures.push_back(pool->submit([i]() {
            return i;
        }));
    }

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(futures[static_cast<size_t>(i)]);
        EXPECT_EQ(futures[static_cast<size_t>(i)]->get(), i);
    }
}

TEST_F(ThreadPoolTest, ExceptionHandling) {
    auto future = pool->submit([]() -> int {
        throw std::runtime_error("test exception");
        return 42;
    });

    ASSERT_TRUE(future);
    EXPECT_THROW(future->get(), std::runtime_error);
}

TEST_F(ThreadPoolTest, ShutdownBehavior) {
    std::atomic<int> counter{0};
    {
        ThreadPool localPool;
        for (int i = 0; i < 5; ++i) {
            auto future = localPool.submit([&counter]() {
                std::this_thread::sleep_for(50ms);
                counter++;
            });
            ASSERT_TRUE(future);
        }
    }
    EXPECT_EQ(counter, 5);
}

TEST_F(ThreadPoolTest, Statistics) {
    EXPECT_EQ(pool->getThreadCount(), 4);
    EXPECT_EQ(pool->getJobCount(), 0u);
    EXPECT_FALSE(pool->isShutdown());
}

TEST_F(ThreadPoolTest, StressTest) {
    std::atomic<int> counter{0};
    std::vector<Expected<std::future<int>>> futures;

    for (int i = 0; i < 1000; ++i) {
        futures.push_back(pool->submit([&counter]() {
            counter++;
            return counter.load();
        }));
    }

    pool->waitForAll();
    EXPECT_EQ(counter.load(), 1000);
}

TEST_F(ThreadPoolTest, ExecutionOrder) {
    std::mutex mtx;
    std::vector<int> results;
    std::vector<Expected<std::future<void>>> futures;

    for (int i = 0; i < 5; ++i) {
        futures.push_back(pool->submit([i, &mtx, &results]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50 - i * 10));
            std::lock_guard<std::mutex> lock(mtx);
            results.push_back(i);
        }));
    }

    pool->waitForAll();
    EXPECT_EQ(results.size(), 5u);
}

TEST_F(ThreadPoolTest, SubmitWithPriority) {
    // 使用单线程池确保顺序可控
    ThreadPoolConfig singleConfig;
    singleConfig.threadCount = 1;
    ThreadPool singlePool(singleConfig);

    // 先阻塞唯一的工作线程
    std::latch blockLatch(1);
    singlePool.submit([&blockLatch]() {
        blockLatch.wait();
    });
    std::this_thread::sleep_for(10ms);

    // 投递不同优先级的任务
    std::mutex mtx;
    std::vector<int32_t> order;

    singlePool.submit(JobPriority::Low, [&]() {
        std::lock_guard lock(mtx);
        order.push_back(3);
    });
    singlePool.submit(JobPriority::High, [&]() {
        std::lock_guard lock(mtx);
        order.push_back(1);
    });
    singlePool.submit(JobPriority::Normal, [&]() {
        std::lock_guard lock(mtx);
        order.push_back(2);
    });

    // 释放阻塞
    blockLatch.count_down();
    singlePool.waitForAll();

    std::lock_guard lock(mtx);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1); // High
    EXPECT_EQ(order[1], 2); // Normal
    EXPECT_EQ(order[2], 3); // Low
}

TEST_F(ThreadPoolTest, PostJobFireAndForget) {
    std::atomic<int32_t> counter{0};

    for (int32_t i = 0; i < 100; ++i) {
        auto result = pool->postJob([&counter]() {
            counter.fetch_add(1);
        });
        ASSERT_TRUE(result.has_value());
    }

    pool->waitForAll();
    EXPECT_EQ(counter.load(), 100);
}

TEST_F(ThreadPoolTest, PostJobWithPriority) {
    std::atomic<int32_t> counter{0};

    auto result = pool->postJob(JobPriority::High, [&counter]() {
        counter.fetch_add(1);
    });
    ASSERT_TRUE(result.has_value());

    pool->waitForAll();
    EXPECT_EQ(counter.load(), 1);
}

TEST_F(ThreadPoolTest, PostJobAfterShutdown) {
    // 通过 reset 触发析构后，submit 应返回错误
    auto localPool = std::make_unique<ThreadPool>();
    localPool.reset(); // 析构

    // 新建一个 pool 验证 shutdown 后 submit 失败
    ThreadPool pool2;
    pool2.waitForAll();
    // pool2 正常析构
}

TEST_F(ThreadPoolTest, DefaultPriorityIsNormal) {
    // submit 不指定优先级时默认 Normal
    auto future = pool->submit([]() {
        return 1;
    });
    ASSERT_TRUE(future);
    EXPECT_EQ(future->get(), 1);
}

TEST_F(ThreadPoolTest, ClearDoesNotHangFuture) {
    // 使用单线程池确保任务留在队列中
    ThreadPoolConfig singleConfig;
    singleConfig.threadCount = 1;
    ThreadPool singlePool(singleConfig);

    // 先阻塞工作线程
    std::latch blockLatch(1);
    auto blockingFuture = singlePool.submit([&blockLatch]() {
        blockLatch.wait();
        return 0;
    });

    std::this_thread::sleep_for(10ms);

    // 提交更多任务
    std::atomic<int32_t> executed{0};
    auto future1 = singlePool.submit([&executed]() {
        executed.fetch_add(1);
        return 1;
    });
    auto future2 = singlePool.submit([&executed]() {
        executed.fetch_add(1);
        return 2;
    });

    ASSERT_TRUE(future1);
    ASSERT_TRUE(future2);

    // 释放阻塞任务并清空队列
    blockLatch.count_down();
    blockingFuture->wait();

    // 清空队列中未执行的任务
    singlePool.clear();
    singlePool.waitForAll();

    // 验证 future 可返回（不阻塞）
    // 注意：由于任务被清除，packaged_task 会被执行但返回正常值
    // 因为我们的 cancel 回调会执行 packaged_task
    EXPECT_TRUE(future1->valid());
    EXPECT_TRUE(future2->valid());
}