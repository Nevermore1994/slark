//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Base.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace slark;

TEST(Base, IsEqualDouble) {
    EXPECT_TRUE(isEqual(1.0, 1.0));
    EXPECT_TRUE(isEqual(0.0, 0.0));
    EXPECT_FALSE(isEqual(1.0, 2.0));
}

TEST(Base, IsEqualFloat) {
    EXPECT_TRUE(isEqual(1.0f, 1.0f));
    EXPECT_FALSE(isEqual(1.0f, 1.1f));
}

TEST(Base, IsEqualNearEpsilon) {
    double a = 1.0;
    double b = 1.0 + std::numeric_limits<double>::epsilon() / 2.0;
    EXPECT_TRUE(isEqual(a, b));
}

TEST(Base, IsEqualCustomEpsilon) {
    EXPECT_TRUE(isEqual(1.0, 1.05, 0.1));
    EXPECT_FALSE(isEqual(1.0, 1.2, 0.1));
}

TEST(Base, IsEqualOrGreater) {
    EXPECT_TRUE(isEqualOrGreater(2.0, 1.0));
    EXPECT_TRUE(isEqualOrGreater(1.0, 1.0));
    EXPECT_FALSE(isEqualOrGreater(0.5, 1.0));
}

TEST(Base, IsEqualOrLess) {
    EXPECT_TRUE(isEqualOrLess(1.0, 2.0));
    EXPECT_TRUE(isEqualOrLess(1.0, 1.0));
    EXPECT_FALSE(isEqualOrLess(2.0, 1.0));
}

TEST(Base, IsEqualNegativeValues) {
    EXPECT_TRUE(isEqual(-1.0, -1.0));
    EXPECT_FALSE(isEqual(-1.0, -2.0));
}

TEST(Base, IsEqualOrGreaterNegative) {
    EXPECT_TRUE(isEqualOrGreater(-1.0, -2.0));
    EXPECT_FALSE(isEqualOrGreater(-3.0, -1.0));
}

TEST(Base, IsEqualOrLessNegative) {
    EXPECT_TRUE(isEqualOrLess(-2.0, -1.0));
    EXPECT_FALSE(isEqualOrLess(-1.0, -3.0));
}

TEST(Base, IsEqualSpecialValues) {
    EXPECT_TRUE(isEqual(0.0, -0.0));
}
