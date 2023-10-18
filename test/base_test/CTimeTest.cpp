//
// Created by Nevermore on 2023/6/21.
// slark main
// Copyright (c) 2023 Nevermore All rights reserved.
//

#include <gtest/gtest.h>
#include "Time.hpp"

using namespace slark;

TEST(equal, CTime) {
    CTime time1(100, 100);
    CTime time2(1.0);
    ASSERT_EQ((time1 == time2), true);
}

TEST(add, CTime) {
    CTime time1(100, 100);
    CTime time2(1.0);
    auto time3 = time1 + time2;
    ASSERT_EQ(time3.second(), 2.0);
    ASSERT_EQ((time3 + time3).second(), 4.0);
}

TEST(sub, CTime) {
    CTime time1(100, 100);
    CTime time2(3.0);
    ASSERT_EQ((time1 - time2).second(), -2.0);
    ASSERT_EQ((time2 - time1).second(), 2.0);
}

TEST(less, CTime) {
    CTime time1(100, 100);
    CTime time2(3.0);
    ASSERT_LE(time1.second(), time2.second());
}

TEST(gerater, CTime) {
    CTime time1(10000, 100);
    CTime time2(3.0);
    ASSERT_GE(time1.second(), time2.second());
}

TEST(equal, CTimeRange) {
    CTimeRange timeRange1(CTime(5.0), CTime(10000, 1000));
    CTimeRange timeRange2(CTime(200, 40), CTime(10));
    ASSERT_EQ(timeRange1 == timeRange2, true);
}

TEST(overloop, CTimeRange) {
    {
        CTimeRange timeRange1(CTime(5.0), CTime(10000, 1000));
        CTimeRange timeRange2(CTime(120, 40), CTime(10));
        ASSERT_EQ(timeRange1.isOverlap(timeRange2), true);
    }

    {
        CTimeRange timeRange1(CTime(5.0), CTime(10000, 1000));
        CTimeRange timeRange2(CTime(50, 2), CTime(10));
        ASSERT_EQ(timeRange1.isOverlap(timeRange2), false);
    }

    {
        CTimeRange timeRange1(CTime(5.0), CTime(10000, 1000));
        CTimeRange timeRange2(CTime(40, 20), CTime(1));
        ASSERT_EQ(timeRange1.isOverlap(timeRange2), false);
    }
}

TEST(less, CTimeRange) {
    {
        CTimeRange timeRange1(CTime(5.0), CTime(10000, 1000));
        CTimeRange timeRange2(CTime(10, 5), CTime(2));
        ASSERT_EQ(timeRange1 > timeRange2, true);
    }

    {
        CTimeRange timeRange1(CTime(5.0), CTime(10000, 1000));
        CTimeRange timeRange2(CTime(40000, 20), CTime(5000, 10));
        ASSERT_EQ(timeRange1 < timeRange2, true);
    }
}



