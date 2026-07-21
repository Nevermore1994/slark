#include "TimerPool.h"
#include "Timer.h"
#include "Time.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <atomic>
#include <thread>

using namespace slark;

namespace {

Time::TimePoint futureTime(uint64_t msFromNow) noexcept {
    return Time::TimePoint::fromMilliSeconds(
        std::chrono::milliseconds(Time::nowTimeStamp().toMilliSeconds().count() + msFromNow));
}

} // namespace

TEST(TimerPoolTest, AddAndExpire) {
    TimerPool pool;
    std::atomic<bool> fired{false};
    TimerEntry entry;
    entry.id = TimerId::next();
    entry.expireTime = futureTime(20);
    entry.task = [&] { fired = true; };
    pool.addTimer(std::move(entry));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::vector<TimerEntry> expired;
    pool.collectExpired(expired);
    EXPECT_GE(expired.size(), 1u);
}

TEST(TimerPoolTest, CancelPreventsExpire) {
    TimerPool pool;
    std::atomic<bool> fired{false};
    TimerEntry entry;
    entry.id = TimerId::next();
    entry.expireTime = futureTime(100);
    entry.task = [&] { fired = true; };
    auto id = pool.addTimer(std::move(entry));
    pool.cancel(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::vector<TimerEntry> expired;
    pool.collectExpired(expired);
    EXPECT_FALSE(fired.load());
}

TEST(TimerPoolTest, MultipleTimers) {
    TimerPool pool;
    std::atomic<int> count{0};
    for (auto ms : {10u, 20u, 30u}) {
        TimerEntry entry;
        entry.id = TimerId::next();
        entry.expireTime = futureTime(ms);
        entry.task = [&] { count++; };
        pool.addTimer(std::move(entry));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    std::vector<TimerEntry> expired;
    pool.collectExpired(expired);
    EXPECT_EQ(expired.size(), 3u);
}

TEST(TimerPoolTest, PeekWaitTime) {
    TimerPool pool;
    EXPECT_EQ(pool.peekWaitTime().count(), 1000);
    TimerEntry entry;
    entry.id = TimerId::next();
    entry.expireTime = futureTime(50);
    entry.task = []{};
    pool.addTimer(std::move(entry));
    auto wait = pool.peekWaitTime();
    EXPECT_GT(wait.count(), 0);
    EXPECT_LE(wait.count(), 50);
}

TEST(TimerPoolTest, Clear) {
    TimerPool pool;
    for (auto ms : {10u, 20u}) {
        TimerEntry entry;
        entry.id = TimerId::next();
        entry.expireTime = futureTime(ms);
        entry.task = []{};
        pool.addTimer(std::move(entry));
    }
    pool.clear();
    std::vector<TimerEntry> expired;
    pool.collectExpired(expired);
    EXPECT_TRUE(expired.empty());
}
