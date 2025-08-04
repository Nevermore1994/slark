#include <gtest/gtest.h>
#include "ThreadPool.hpp"
#include <chrono>
#include <atomic>

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
    auto future = pool->submit([](){ return 42; });
    ASSERT_TRUE(future);
    EXPECT_EQ(future->get(), 42);
}

TEST_F(ThreadPoolTest, MultipleTasksExecution) {
    std::vector<std::expected<std::future<int>, bool>> futures;
    for (int i = 0; i < 5; ++i) {
        futures.push_back(pool->submit([i](){ return i; }));
    }

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(futures[i]);
        EXPECT_EQ(futures[i]->get(), i);
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
    EXPECT_EQ(pool->getTaskCount(), 0);

    auto future = pool->submit([]() {
        std::this_thread::sleep_for(100ms);
    });

    EXPECT_EQ(pool->getTaskCount(), 1);
    EXPECT_FALSE(pool->isShutdown());
}


TEST_F(ThreadPoolTest, StressTest) {
    std::atomic<int> counter{0};
    std::vector<std::expected<std::future<int>, bool>> futures;

    for (int i = 0; i < 1000; ++i) {
        futures.push_back(pool->submit([&counter]() {
            counter++;
            return counter.load();
        }));
    }
    
    int successCount = 0;
    for (const auto& future : futures) {
        if (future) {
            successCount++;
        }
    }
    std::this_thread::sleep_for(1s);
    EXPECT_EQ(counter.load(), successCount);
}

TEST_F(ThreadPoolTest, ExecutionOrder) {
    std::mutex mtx;
    std::vector<int> results;
    std::vector<std::expected<std::future<void>, bool>> futures;

    for (int i = 0; i < 5; ++i) {
        futures.push_back(pool->submit([i, &mtx, &results]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50 - i * 10));
            std::lock_guard<std::mutex> lock(mtx);
            results.push_back(i);
        }));
    }

    pool->waitForAll();
    EXPECT_EQ(results.size(), 5);
}