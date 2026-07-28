//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "MPSCQueue.hpp"

#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace slark;

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
            while (!startFlag.load()) {
            }
            for (int j = 0; j < kItemsPerThread; ++j) {
                queue.push(i * kItemsPerThread + j);
            }
        });
    }

    std::unordered_set<int> results;
    std::thread consumer([&]() {
        int totalItems = kThreads * kItemsPerThread;
        int val;
        while (static_cast<int>(results.size()) < totalItems) {
            if (queue.tryPop(val)) {
                results.insert(val);
            }
        }
    });

    startFlag.store(true);
    for (auto& t : producers) {
        t.join();
    }
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
            while (!startFlag.load()) {
            }

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

    while (producersReady.load() < kProducerThreads) {
    }
    startFlag.store(true);

    for (auto& t : producers) {
        t.join();
    }
    consumer.join();

    ASSERT_EQ(poppedItems.load(), kTotalItems);
    for (int tid = 0; tid < kProducerThreads; ++tid) {
        ASSERT_EQ(resultMap[tid].size(), static_cast<size_t>(kItemsPerProducer));
        for (int i = 0; i < kItemsPerProducer; ++i) {
            EXPECT_EQ(resultMap[tid][static_cast<size_t>(i)], i)
                << "Thread " << tid << " value mismatch at index " << i;
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
    constexpr int kN = 100000;
    for (int i = 0; i < kN; ++i) {
        queue.push(i);
    }
    int64_t sum = 0;
    int out;
    for (int i = 0; i < kN; ++i) {
        EXPECT_TRUE(queue.tryPop(out));
        sum += out;
    }
    EXPECT_EQ(sum, static_cast<int64_t>(kN - 1) * kN / 2);
    EXPECT_FALSE(queue.tryPop(out));
}

TEST(MPSCQueueTest, DestructWithItems) {
    auto* queue = new MPSCQueue<int>();
    for (int i = 0; i < 10; ++i) {
        queue->push(i);
    }
    delete queue;
}

TEST(MPSCQueueTest, SingleProducerSingleConsumer) {
    MPSCQueue<int> queue;
    std::thread producer([&]() {
        for (int i = 0; i < 1000; ++i) {
            queue.push(i);
        }
    });
    int count = 0, out;
    while (count < 1000) {
        if (queue.tryPop(out)) {
            ++count;
        }
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

    ThrowOnCopy(const ThrowOnCopy&) {
        throw std::runtime_error("copy");
    }

    ThrowOnCopy& operator=(const ThrowOnCopy&) = delete;

    ThrowOnCopy& operator=(ThrowOnCopy&&) {
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
            for (int i = 0; i < kItems; ++i) {
                queue.push(t * kItems + i);
            }
        });
    }
    for (auto& t : producers) {
        t.join();
    }
    int out, count = 0;
    while (queue.tryPop(out)) {
        ++count;
    }
    EXPECT_EQ(count, kThreads * kItems);
    EXPECT_FALSE(queue.tryPop(out));
}

TEST(MPSCQueueTest, BatchPushPop) {
    MPSCQueue<int> queue;
    for (int i = 0; i < 100; ++i) {
        queue.push(i);
    }
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

            while (!startFlag.load()) {
            }

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
    for (auto& t : producers) {
        t.join();
    }
    consumer.join();

    EXPECT_EQ(results.size(), kTotalItems);
    for (int i = 0; i < kTotalItems; ++i) {
        EXPECT_TRUE(results.contains(i));
    }
}

TEST(MPSCQueueTest, TryPopAll) {
    MPSCQueue<int> queue;
    for (int i = 0; i < 100; ++i) {
        queue.push(i);
    }
    auto all = queue.tryPopAll();
    EXPECT_EQ(all.size(), 100u);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(all[static_cast<size_t>(i)], i);
    }
    EXPECT_TRUE(queue.empty());
}

TEST(MPSCQueueTest, TryPopAllEmpty) {
    MPSCQueue<int> queue;
    auto all = queue.tryPopAll();
    EXPECT_TRUE(all.empty());
}

TEST(MPSCQueueTest, SizeTracking) {
    MPSCQueue<int> queue;
    EXPECT_EQ(queue.size(), 0u);
    for (int i = 0; i < 50; ++i) {
        queue.push(i);
    }
    EXPECT_EQ(queue.size(), 50u);
    int out;
    for (int i = 0; i < 20; ++i) {
        static_cast<void>(queue.tryPop(out));
    }
    EXPECT_EQ(queue.size(), 30u);
    (void)queue.tryPopAll();
    EXPECT_EQ(queue.size(), 0u);
}

TEST(MPSCQueueTest, TryPopAllMultiProducer) {
    MPSCQueue<int> queue;
    constexpr int32_t kThreads = 4;
    constexpr int32_t kItemsPerThread = 1000;
    std::vector<std::thread> producers;
    for (int32_t t = 0; t < kThreads; ++t) {
        producers.emplace_back([&, t]() {
            for (int32_t i = 0; i < kItemsPerThread; ++i) {
                queue.push(t * kItemsPerThread + i);
            }
        });
    }
    for (auto& t : producers) {
        t.join();
    }
    auto all = queue.tryPopAll();
    EXPECT_EQ(all.size(), kThreads * kItemsPerThread);
    std::unordered_set<int> unique(all.begin(), all.end());
    EXPECT_EQ(unique.size(), kThreads * kItemsPerThread);
}

// 测试非默认可构造类型
struct NonDefaultConstructible {
    int value;

    explicit NonDefaultConstructible(int v) : value(v) {}

    NonDefaultConstructible(const NonDefaultConstructible&) = delete;

    NonDefaultConstructible(NonDefaultConstructible&& other) noexcept : value(other.value) {
        other.value = -1;
    }

    NonDefaultConstructible& operator=(const NonDefaultConstructible&) = delete;

    NonDefaultConstructible& operator=(NonDefaultConstructible&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
};

TEST(MPSCQueueTest, NonDefaultConstructibleType) {
    // 验证非默认可构造类型可以编译和使用
    MPSCQueue<NonDefaultConstructible> queue;
    queue.push(NonDefaultConstructible(42));
    queue.push(NonDefaultConstructible(100));

    // tryPop 需要默认构造，所以不测试
    auto all = queue.tryPopAll();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].value, 42);
    EXPECT_EQ(all[1].value, 100);
}

TEST(MPSCQueueTest, TryPopAllWithNonDefaultConstructible) {
    MPSCQueue<NonDefaultConstructible> queue;
    constexpr int32_t kCount = 100;

    for (int32_t i = 0; i < kCount; ++i) {
        queue.push(NonDefaultConstructible(i));
    }

    auto all = queue.tryPopAll();
    EXPECT_EQ(all.size(), static_cast<size_t>(kCount));

    for (int32_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(all[static_cast<size_t>(i)].value, i);
    }
}