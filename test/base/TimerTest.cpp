//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <format>
#include <latch>
#include <vector>

#include "TimerManager.h"
#include "TimerPool.h"
#include "Util.h"

using namespace slark;
using namespace std::chrono_literals;

TEST(Timer, delay) {
    auto now = Time::nowTimeStamp();
    TimerManager::shareInstance().runAfter(10ms, [&] {
        auto t = Time::nowTimeStamp() - now;
        std::println("delay {}", t.toMilliSeconds());
        ASSERT_GE(t, 10.0);
    });
    std::this_thread::sleep_for(0.5s);
    TimerManager::shareInstance().clear();
}

TEST(Timer, loop) {
    auto now = Time::nowTimeStamp();
    auto timerId = TimerManager::shareInstance().runLoop(10ms, [&] {
        auto n = Time::nowTimeStamp();
        auto t = n - now;
        std::println("delay {}", t.toMilliSeconds());
        ASSERT_GE(t, 10.0);
        now = n;
    });
    std::this_thread::sleep_for(3s);
    TimerManager::shareInstance().cancel(timerId);
    TimerManager::shareInstance().clear();
}

TEST(Timer, exceptionDoesNotKillThread) {
    // 异常不应导致 TimerManager 线程终止
    // 清理之前测试残留的定时器，防止干扰
    TimerManager::shareInstance().clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::atomic<int32_t> counter{0};

    // 第一个定时器会抛出异常
    [[maybe_unused]] auto id1 = TimerManager::shareInstance().runAfter(10ms, [&counter]() {
        counter.fetch_add(1);
        throw std::runtime_error("test exception");
    });

    // 第二个定时器在第一个之后执行
    [[maybe_unused]] auto id2 = TimerManager::shareInstance().runAfter(50ms, [&counter]() {
        counter.fetch_add(1);
    });

    std::this_thread::sleep_for(200ms);

    // 两个任务都应该执行（异常不应阻止第二个任务）
    EXPECT_EQ(counter.load(), 2);

    // 再添加一个定时器验证 TimerManager 仍然工作
    std::atomic<bool> thirdExecuted{false};
    TimerManager::shareInstance().runAfter(10ms, [&thirdExecuted]() {
        thirdExecuted.store(true);
    });

    std::this_thread::sleep_for(100ms);
    EXPECT_TRUE(thirdExecuted.load());

    // 清理定时器，避免后续测试被残留回调干扰
    TimerManager::shareInstance().clear();
}

namespace {

TimerId addAfter(TimerPool& pool, std::chrono::milliseconds ms, TimerTask func) {
    auto id = TimerId::next();
    TimerEntry entry{
        .id = id,
        .expireTime = Time::nowTimeStamp() + ms,
        .task = std::move(func),
        .isLoop = false,
        .loopInterval = {},
        .isValid = true,
    };
    pool.addTimer(std::move(entry));
    return id;
}

TimerId addLoop(TimerPool& pool, std::chrono::milliseconds ms, TimerTask func) {
    auto id = TimerId::next();
    TimerEntry entry{
        .id = id,
        .expireTime = Time::nowTimeStamp() + ms,
        .task = std::move(func),
        .isLoop = true,
        .loopInterval = ms,
        .isValid = true,
    };
    pool.addTimer(std::move(entry));
    return id;
}

void tickPool(TimerPool& pool) {
    std::vector<TimerEntry> expired;
    pool.collectExpired(expired);
    for (auto& entry : expired) {
        if (entry.isValid && entry.task) {
            entry.task();
            if (entry.isLoop) {
                entry.expireTime = Time::nowTimeStamp() + entry.loopInterval;
                entry.isValid = true;
                pool.reinsert(std::move(entry));
            }
        }
    }
}

}  // anonymous namespace

TEST(TimerPool, runAfter) {
    TimerPool pool;
    std::atomic<bool> fired{false};

    addAfter(pool, 10ms, [&fired]() {
        fired.store(true);
    });

    for (int32_t i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(1ms);
        tickPool(pool);
    }
    ASSERT_TRUE(fired.load());
}

TEST(TimerPool, runLoop) {
    TimerPool pool;
    std::atomic<int32_t> counter{0};

    auto id = addLoop(pool, 5ms, [&counter]() {
        counter.fetch_add(1);
    });

    for (int32_t i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(1ms);
        tickPool(pool);
    }
    pool.cancel(id);

    ASSERT_GE(counter.load(), 5);
}

TEST(TimerPool, cancel) {
    TimerPool pool;
    std::atomic<bool> fired{false};

    auto id = addAfter(pool, 10ms, [&fired]() {
        fired.store(true);
    });

    pool.cancel(id);

    for (int32_t i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(1ms);
        tickPool(pool);
    }
    ASSERT_FALSE(fired.load());
}

TEST(TimerPool, manyTimers) {
    TimerPool pool;
    constexpr int32_t kCount = 100;
    std::atomic<int32_t> counter{0};

    for (int32_t i = 0; i < kCount; ++i) {
        addAfter(pool, std::chrono::milliseconds(i + 1), [&counter]() {
            counter.fetch_add(1);
        });
    }

    for (int32_t i = 0; i < 150; ++i) {
        std::this_thread::sleep_for(1ms);
        tickPool(pool);
    }
    ASSERT_EQ(counter.load(), kCount);
}

TEST(TimerPool, batchExpiry) {
    TimerPool pool;
    std::atomic<int32_t> counter{0};

    for (int32_t i = 0; i < 10; ++i) {
        addAfter(pool, 5ms, [&counter]() {
            counter.fetch_add(1);
        });
    }

    std::this_thread::sleep_for(10ms);
    tickPool(pool);

    ASSERT_EQ(counter.load(), 10);
}

TEST(TimerPool, timerIdUnique) {
    TimerPool pool;
    std::vector<TimerId> ids;

    for (int32_t i = 0; i < 100; ++i) {
        auto id = addAfter(pool, std::chrono::milliseconds(100 + i), []() {
        });
        ids.push_back(id);
    }

    std::sort(ids.begin(), ids.end(), [](const TimerId& a, const TimerId& b) {
        return a.id() < b.id();
    });
    auto last = std::unique(ids.begin(), ids.end(), [](const TimerId& a, const TimerId& b) {
        return a.id() == b.id();
    });
    ASSERT_EQ(std::distance(ids.begin(), last), 100);
}

TEST(TimerPool, clearStopsAll) {
    TimerPool pool;
    std::atomic<int32_t> counter{0};

    for (int32_t i = 0; i < 10; ++i) {
        addAfter(pool, 10ms, [&counter]() {
            counter.fetch_add(1);
        });
    }

    pool.clear();

    for (int32_t i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(1ms);
        tickPool(pool);
    }
    ASSERT_EQ(counter.load(), 0);
}
