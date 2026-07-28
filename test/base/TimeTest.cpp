//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Time.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace slark;

// --- TimeDelta Tests ---

TEST(TimeDelta, DefaultConstruction) {
    Time::TimeDelta td;
    EXPECT_EQ(td.point(), 0);
}

TEST(TimeDelta, ConstructFromMicroseconds) {
    Time::TimeDelta td(1000);
    EXPECT_EQ(td.point(), 1000);
}

TEST(TimeDelta, ToMilliSeconds) {
    Time::TimeDelta td(5000);
    EXPECT_EQ(td.toMilliSeconds().count(), 5);
}

TEST(TimeDelta, Second) {
    Time::TimeDelta td(1000000);
    EXPECT_DOUBLE_EQ(td.second(), 1.0);
}

TEST(TimeDelta, FromMilliSeconds) {
    auto td = Time::TimeDelta::fromMilliSeconds(std::chrono::milliseconds(100));
    EXPECT_EQ(td.point(), 100000);
}

TEST(TimeDelta, FromMicroSeconds) {
    auto td = Time::TimeDelta::fromMicroSeconds(std::chrono::microseconds(500));
    EXPECT_EQ(td.point(), 500);
}

TEST(TimeDelta, FromSeconds) {
    auto td = Time::TimeDelta::fromSeconds(2.5);
    EXPECT_EQ(td.point(), 2500000);
}

TEST(TimeDelta, Addition) {
    Time::TimeDelta a(1000);
    Time::TimeDelta b(2000);
    auto c = a + b;
    EXPECT_EQ(c.point(), 3000);
}

TEST(TimeDelta, AdditionAssign) {
    Time::TimeDelta a(1000);
    a += Time::TimeDelta(500);
    EXPECT_EQ(a.point(), 1500);
}

TEST(TimeDelta, Subtraction) {
    Time::TimeDelta a(3000);
    Time::TimeDelta b(1000);
    auto c = a - b;
    EXPECT_EQ(c.point(), 2000);
}

TEST(TimeDelta, SubtractionAssign) {
    Time::TimeDelta a(3000);
    a -= Time::TimeDelta(1000);
    EXPECT_EQ(a.point(), 2000);
}

TEST(TimeDelta, NegativeResult) {
    Time::TimeDelta a(1000);
    Time::TimeDelta b(3000);
    auto c = a - b;
    EXPECT_EQ(c.point(), -2000);
}

TEST(TimeDelta, Comparison) {
    Time::TimeDelta a(1000);
    Time::TimeDelta b(2000);
    Time::TimeDelta c(1000);

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a == c);
    EXPECT_TRUE(a <= c);
    EXPECT_TRUE(a >= c);
    EXPECT_TRUE(a != b);
}

// --- TimePoint Tests ---

TEST(TimePoint, DefaultConstruction) {
    Time::TimePoint tp;
    EXPECT_EQ(tp.point(), 0u);
}

TEST(TimePoint, ConstructFromValue) {
    Time::TimePoint tp(1000000u);
    EXPECT_EQ(tp.point(), 1000000u);
}

TEST(TimePoint, ToMilliSeconds) {
    Time::TimePoint tp(5000u);
    EXPECT_EQ(tp.toMilliSeconds().count(), 5);
}

TEST(TimePoint, Second) {
    Time::TimePoint tp(1000000u);
    EXPECT_DOUBLE_EQ(tp.second(), 1.0);
}

TEST(TimePoint, FromMilliSeconds) {
    auto tp = Time::TimePoint::fromMilliSeconds(std::chrono::milliseconds(100));
    EXPECT_EQ(tp.point(), 100000u);
}

TEST(TimePoint, FromMicroSeconds) {
    auto tp = Time::TimePoint::fromMicroSeconds(std::chrono::microseconds(500));
    EXPECT_EQ(tp.point(), 500u);
}

TEST(TimePoint, FromSeconds) {
    auto tp = Time::TimePoint::fromSeconds(2.5);
    EXPECT_EQ(tp.point(), 2500000u);
}

TEST(TimePoint, AddTimeDelta) {
    Time::TimePoint tp(1000u);
    Time::TimeDelta td(500);
    auto result = tp + td;
    EXPECT_EQ(result.point(), 1500u);
}

TEST(TimePoint, AddTimeDeltaAssign) {
    Time::TimePoint tp(1000u);
    tp += Time::TimeDelta(500);
    EXPECT_EQ(tp.point(), 1500u);
}

TEST(TimePoint, SubtractTimeDelta) {
    Time::TimePoint tp(3000u);
    Time::TimeDelta td(1000);
    auto result = tp - td;
    EXPECT_EQ(result.point(), 2000u);
}

TEST(TimePoint, SubtractTimeDeltaAssign) {
    Time::TimePoint tp(3000u);
    tp -= Time::TimeDelta(1000);
    EXPECT_EQ(tp.point(), 2000u);
}

TEST(TimePoint, SubtractTimePoint) {
    Time::TimePoint a(5000u);
    Time::TimePoint b(3000u);
    auto delta = a - b;
    EXPECT_EQ(delta.point(), 2000);
}

TEST(TimePoint, AddMilliseconds) {
    Time::TimePoint tp(1000000u);
    auto result = tp + std::chrono::milliseconds(100);
    EXPECT_EQ(result.point(), 1100000u);
}

TEST(TimePoint, AddMillisecondsAssign) {
    Time::TimePoint tp(1000000u);
    tp += std::chrono::milliseconds(100);
    EXPECT_EQ(tp.point(), 1100000u);
}

TEST(TimePoint, AddTimePoint) {
    Time::TimePoint a(1000u);
    Time::TimePoint b(2000u);
    auto result = a + b;
    EXPECT_EQ(result.point(), 3000u);
}

TEST(TimePoint, Comparison) {
    Time::TimePoint a(1000u);
    Time::TimePoint b(2000u);
    Time::TimePoint c(1000u);

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(a == c);
    EXPECT_TRUE(a <= c);
    EXPECT_TRUE(a >= c);
    EXPECT_TRUE(a != b);
}

// --- Time static methods ---

TEST(Time, NowTimeStamp) {
    auto t1 = Time::nowTimeStamp();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto t2 = Time::nowTimeStamp();
    EXPECT_GT(t2, t1);
}

TEST(Time, NowTimeStampMonotonic) {
    auto prev = Time::nowTimeStamp();
    for (int32_t i = 0; i < 100; ++i) {
        auto now = Time::nowTimeStamp();
        EXPECT_GE(now, prev);
        prev = now;
    }
}

TEST(Time, NowUTCTime) {
    auto utc = Time::nowUTCTime();
    EXPECT_GT(utc.count(), 0);
}

TEST(Time, OffsetFromUTC) {
    auto offset = Time::offsetFromUTC();
    EXPECT_GE(offset.count(), -12 * 3600);
    EXPECT_LE(offset.count(), 14 * 3600);
}

TEST(Time, LocalTimeStr) {
    auto str = Time::localTimeStr();
    EXPECT_FALSE(str.empty());
}

TEST(Time, LocalShortTimeStr) {
    auto str = Time::localShortTimeStr();
    EXPECT_FALSE(str.empty());
    EXPECT_NE(str.find('-'), std::string::npos);
    EXPECT_NE(str.find(':'), std::string::npos);
}
