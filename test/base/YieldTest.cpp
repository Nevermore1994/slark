// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "Yield.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "CoScope.hpp"
#include "CoroutineTestSupport.h"
#include "Task.hpp"

using namespace std::chrono_literals;

namespace {

slark::Task<void> yieldTimes(int times, std::atomic<int>& steps) {
    for (int i = 0; i < times; ++i) {
        co_await slark::yield();
        ++steps;
    }
}

}  // namespace

TEST(YieldTest, AlwaysSuspendsBeforeContinuing) {
    EXPECT_FALSE(slark::yield().await_ready());
}

TEST(YieldTest, ContinuesAfterEveryYield) {
    slark::coro::AsyncScope scope;
    std::atomic<int> steps{0};
    scope.spawn(yieldTimes(100, steps));
    ASSERT_TRUE(slark::test::joinScope(scope));
    EXPECT_EQ(steps.load(), 100);
}

TEST(YieldTest, DestroyingASuspendedCoroutineIsSafe) {
    std::atomic<int> steps{0};
    for (int round = 0; round < 200; ++round) {
        auto task = yieldTimes(3, steps);
        task.resume();
    }
    std::this_thread::sleep_for(50ms);
}
