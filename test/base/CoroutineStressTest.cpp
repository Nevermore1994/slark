//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Awaitable.hpp"
#include "CoroutineTestSupport.h"
#include "CoWhenAll.hpp"
#include "Task.hpp"

using namespace std::chrono_literals;
using slark::CompletionAwaiter;
using slark::IOResult;
using slark::Task;
using slark::TimerAwaiter;
using slark::test::ManualIO;
using slark::test::waitUntilDone;

namespace {

constexpr std::size_t kConcurrent = 100;
constexpr std::size_t kProducers = 4;

Task<int> awaitValue(std::shared_ptr<CompletionAwaiter<int>> gate) {
    co_return co_await *gate;
}

Task<void> sleepThenCount(std::chrono::milliseconds delay, std::atomic<int>* done) {
    co_await TimerAwaiter(delay);
    done->fetch_add(1, std::memory_order_relaxed);
}

Task<IOResult<std::string>> awaitManual(std::shared_ptr<ManualIO> io) {
    co_return co_await *io;
}

Task<int> nestedChain(int depth, std::shared_ptr<CompletionAwaiter<int>> bottom) {
    if (depth == 0) {
        co_return co_await *bottom;
    }
    const int below = co_await nestedChain(depth - 1, bottom);
    co_return below + 1;
}

Task<void> countOnce(std::atomic<int>* count) {
    count->fetch_add(1, std::memory_order_relaxed);
    co_return;
}

Task<void> whenAllInSeries(int rounds, std::atomic<int>* count) {
    for (int i = 0; i < rounds; ++i) {
        co_await slark::coro::when_all(countOnce(count), countOnce(count));
    }
}

}  // namespace

TEST(CoroutineStressTest, ManyConcurrentCoroutinesAllResumeWithTheirOwnValue) {
    std::vector<std::shared_ptr<CompletionAwaiter<int>>> gates;
    std::vector<Task<int>> tasks;
    gates.reserve(kConcurrent);
    tasks.reserve(kConcurrent);
    for (std::size_t i = 0; i < kConcurrent; ++i) {
        gates.push_back(std::make_shared<CompletionAwaiter<int>>());
        tasks.push_back(awaitValue(gates.back()));
        tasks.back().resume();
        ASSERT_FALSE(tasks.back().isDone());
    }

    std::vector<std::thread> producers;
    for (std::size_t p = 0; p < kProducers; ++p) {
        producers.emplace_back([&gates, p] {
            for (std::size_t i = p; i < kConcurrent; i += kProducers) {
                gates[i]->complete(static_cast<int>(i));
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    for (std::size_t i = 0; i < kConcurrent; ++i) {
        ASSERT_TRUE(waitUntilDone(tasks[i])) << "coroutine " << i;
        EXPECT_EQ(tasks[i].result(), static_cast<int>(i));
    }
}

TEST(CoroutineStressTest, ManyConcurrentTimersAllFire) {
    std::atomic<int> done{0};
    std::vector<Task<void>> tasks;
    tasks.reserve(kConcurrent);
    for (std::size_t i = 0; i < kConcurrent; ++i) {
        tasks.push_back(sleepThenCount(std::chrono::milliseconds(static_cast<int>(i % 20)), &done));
        tasks.back().resume();
    }
    for (auto& task : tasks) {
        ASSERT_TRUE(waitUntilDone(task, 5s));
    }
    EXPECT_EQ(done.load(), static_cast<int>(kConcurrent));
}

TEST(CoroutineStressTest, ManyConcurrentAsyncIOOperationsCompleteExactlyOnce) {
    std::vector<std::shared_ptr<ManualIO>> ios;
    std::vector<Task<IOResult<std::string>>> tasks;
    ios.reserve(kConcurrent);
    tasks.reserve(kConcurrent);
    for (std::size_t i = 0; i < kConcurrent; ++i) {
        ios.push_back(std::make_shared<ManualIO>());
        tasks.push_back(awaitManual(ios.back()));
        tasks.back().resume();
        ASSERT_FALSE(tasks.back().isDone());
    }

    std::vector<std::thread> producers;
    for (std::size_t p = 0; p < kProducers; ++p) {
        producers.emplace_back([&ios, p] {
            for (std::size_t i = p; i < kConcurrent; i += kProducers) {
                ios[i]->finish("value-" + std::to_string(i));
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    for (std::size_t i = 0; i < kConcurrent; ++i) {
        ASSERT_TRUE(waitUntilDone(tasks[i])) << "operation " << i;
        const auto outcome = tasks[i].result();
        ASSERT_TRUE(outcome.has_value()) << "operation " << i;
        EXPECT_EQ(outcome.value(), "value-" + std::to_string(i));
    }
}

TEST(CoroutineStressTest, FiftyLevelsOfNestedAwaitResumeThroughTheChain) {
    auto bottom = std::make_shared<CompletionAwaiter<int>>();
    auto root = nestedChain(50, bottom);
    root.resume();
    ASSERT_FALSE(root.isDone());

    bottom->complete(0);
    ASSERT_TRUE(waitUntilDone(root));
    EXPECT_EQ(root.result(), 50);
}

TEST(CoroutineStressTest, TenWhenAllCallsInSeriesRunEveryChild) {
    std::atomic<int> count{0};
    auto root = whenAllInSeries(10, &count);
    root.resume();
    ASSERT_TRUE(waitUntilDone(root, 5s));
    EXPECT_EQ(count.load(), 20);
}
