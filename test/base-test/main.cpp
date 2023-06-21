//
// Created by Nevermore on 2023/6/21.
// slark main
// Copyright (c) 2023 Nevermore All rights reserved.
//

#include <gtest/gtest.h>

#include <cstdio>

TEST(Add, integer) {
    constexpr int expected = 3;
    constexpr int actual = 1 + 2;
    ASSERT_EQ(expected, actual);
}
