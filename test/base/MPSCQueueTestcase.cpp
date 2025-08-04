#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_set>
#include <memory>
#include <string>
#include <random>
#include <algorithm>
#include <mutex>
#include "MPSCQueue.hpp"


TEST(MPSCQueueTest, SingleThreadPushPop) {
    MPSCQueue<int> queue;
    queue.push(42);

    int out = 0;
    EXPECT_TRUE(queue.tryPop(out));
    EXPECT_EQ(out, 42);

    EXPECT_FALSE(queue.tryPop(out));
}

TEST(MPSCQueueTest, MultiProducerSingleConsumer) {
    MPSCQueue<int> queue;
    constexpr int kThreads = 4;
    constexpr int kItemsPerThread = 1000;

    std::atomic<bool> startFlag{false};
    std::vector<std::thread> producers;

    for (int i = 0; i < kThreads; ++i) {
        producers.emplace_back([&, i]() {
            while (!startFlag.load()) {}
            for (int j = 0; j < kItemsPerThread; ++j) {
                queue.push(i * kItemsPerThread + j);
            }
        });
    }

    std::unordered_set<int> results;
    std::thread consumer([&]() {
        int totalItems = kThreads * kItemsPerThread;
        int val;
        while (results.size() < totalItems) {
            if (queue.tryPop(val)) {
                results.insert(val);
            }
        }
    });

    startFlag.store(true);
    for (auto& t : producers) t.join();
    consumer.join();


    EXPECT_EQ(results.size(), kThreads * kItemsPerThread);
    for (int i = 0; i < kThreads * kItemsPerThread; ++i) {
        EXPECT_TRUE(results.count(i));
    }
}

TEST(MPSCQueueTest, PopFromEmptyQueue) {
    MPSCQueue<int> queue;
    int out = 0;
    EXPECT_FALSE(queue.tryPop(out));
}

TEST(MPSCQueueTest, StressTest_MultiProducerSingleConsumer) {
    constexpr int kProducerThreads = 16;
    constexpr int kItemsPerProducer = 1000000;
    constexpr int kTotalItems = kProducerThreads * kItemsPerProducer;

    MPSCQueue<std::pair<int, int>> queue;
    std::atomic<bool> startFlag{false};
    std::atomic<int> producersReady{0};
    
    std::vector<std::thread> producers;
    for (int tid = 0; tid < kProducerThreads; ++tid) {
        producers.emplace_back([&, tid]() {
            producersReady.fetch_add(1);
            while (!startFlag.load()) {}

            for (int i = 0; i < kItemsPerProducer; ++i) {
                queue.push(std::make_pair(tid, i));
            }
        });
    }
    
    std::unordered_map<int, std::vector<int>> resultMap;
    std::mutex resultMutex;

    std::atomic<int> poppedItems{0};
    std::thread consumer([&]() {
        while (poppedItems.load() < kTotalItems) {
            std::pair<int, int> val;
            if (queue.tryPop(val)) {
                poppedItems.fetch_add(1);

                std::lock_guard<std::mutex> lock(resultMutex);
                resultMap[val.first].push_back(val.second);
            }
        }
    });
    
    while (producersReady.load() < kProducerThreads) {}
    startFlag.store(true);

    for (auto& t : producers) t.join();
    consumer.join();
    
    ASSERT_EQ(poppedItems.load(), kTotalItems);
    for (int tid = 0; tid < kProducerThreads; ++tid) {
        ASSERT_EQ(resultMap[tid].size(), kItemsPerProducer);
        for (int i = 0; i < kItemsPerProducer; ++i) {
            EXPECT_EQ(resultMap[tid][i], i) << "Thread " << tid << " value mismatch at index " << i;
        }
    }
}

TEST(MPSCQueueTest, PopEmptyMultipleTimes) {
    MPSCQueue<int> queue;
    int out = 123;
    for (int i = 0; i < 10; ++i) {
        EXPECT_FALSE(queue.tryPop(out));
    }
}

TEST(MPSCQueueTest, PushPopAlternating) {
    MPSCQueue<int> queue;
    for (int i = 0; i < 100; ++i) {
        queue.push(i);
        int out = -1;
        EXPECT_TRUE(queue.tryPop(out));
        EXPECT_EQ(out, i);
    }
    int out = -1;
    EXPECT_FALSE(queue.tryPop(out));
}

TEST(MPSCQueueTest, PushNullptrSharedPtr) {
    MPSCQueue<std::shared_ptr<int>> queue;
    queue.push(nullptr);
    std::shared_ptr<int> out;
    EXPECT_TRUE(queue.tryPop(out));
    EXPECT_EQ(out, nullptr);
    EXPECT_FALSE(queue.tryPop(out));
}

TEST(MPSCQueueTest, LargeVolume) {
    MPSCQueue<int> queue;
    constexpr int N = 100000;
    for (int i = 0; i < N; ++i) queue.push(i);
    int sum = 0, out;
    for (int i = 0; i < N; ++i) {
        EXPECT_TRUE(queue.tryPop(out));
        sum += out;
    }
    EXPECT_EQ(sum, (N - 1) * N / 2);
    EXPECT_FALSE(queue.tryPop(out));
}


TEST(MPSCQueueTest, DestructWithItems) {
    auto* queue = new MPSCQueue<int>();
    for (int i = 0; i < 10; ++i) queue->push(i);
    delete queue;
}


TEST(MPSCQueueTest, SingleProducerSingleConsumer) {
    MPSCQueue<int> queue;
    std::thread producer([&]() {
        for (int i = 0; i < 1000; ++i) queue.push(i);
    });
    int count = 0, out;
    while (count < 1000) {
        if (queue.tryPop(out)) ++count;
    }
    producer.join();
    EXPECT_EQ(count, 1000);
}

TEST(MPSCQueueTest, StringType) {
    MPSCQueue<std::string> queue;
    queue.push(std::string("hello"));
    std::string out;
    EXPECT_TRUE(queue.tryPop(out));
    EXPECT_EQ(out, "hello");
}

struct ThrowOnCopy {
    ThrowOnCopy() = default;
    ThrowOnCopy(const ThrowOnCopy&) { throw std::runtime_error("copy"); }
    ThrowOnCopy& operator=(const ThrowOnCopy&) = delete;
    ThrowOnCopy& operator=(ThrowOnCopy&& rhs) {
        return *this;
    }
};

TEST(MPSCQueueTest, ExceptionSafety) {
    MPSCQueue<ThrowOnCopy> queue;
    EXPECT_THROW(queue.push(ThrowOnCopy()), std::runtime_error);
}

TEST(MPSCQueueTest, MultiProducerPopToEmpty) {
    MPSCQueue<int> queue;
    constexpr int kThreads = 4, kItems = 1000;
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&, t]() {
            for (int i = 0; i < kItems; ++i) queue.push(t * kItems + i);
        });
    }
    for (auto& t : producers) t.join();
    int out, count = 0;
    while (queue.tryPop(out)) ++count;
    EXPECT_EQ(count, kThreads * kItems);
    EXPECT_FALSE(queue.tryPop(out));
}

TEST(MPSCQueueTest, BatchPushPop) {
    MPSCQueue<int> queue;
    for (int i = 0; i < 100; ++i) queue.push(i);
    int out;
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(queue.tryPop(out));
        EXPECT_EQ(out, i);
    }
    EXPECT_FALSE(queue.tryPop(out));
}


TEST(MPSCQueueTest, MultiProducerUnorderedPush) {
    constexpr int kThreads = 8;
    constexpr int kItemsPerThread = 1000;
    constexpr int kTotalItems = kThreads * kItemsPerThread;

    MPSCQueue<int> queue;
    std::atomic<bool> startFlag{false};
    std::vector<std::thread> producers;
    
    for (int tid = 0; tid < kThreads; ++tid) {
        producers.emplace_back([&, tid]() {
            std::vector<int> data;
            for (int i = 0; i < kItemsPerThread; ++i) {
                data.push_back(tid * kItemsPerThread + i);
            }
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(data.begin(), data.end(), g);

            while (!startFlag.load()) {}

            for (int v : data) {
                queue.push(v);
            }
        });
    }
    
    std::unordered_set<int> results;
    std::thread consumer([&]() {
        int val;
        while (results.size() < kTotalItems) {
            if (queue.tryPop(val)) {
                results.insert(val);
            }
        }
    });

    startFlag.store(true);
    for (auto& t : producers) t.join();
    consumer.join();
    
    EXPECT_EQ(results.size(), kTotalItems);
    for (int i = 0; i < kTotalItems; ++i) {
        EXPECT_TRUE(results.contains(i));
    }
}