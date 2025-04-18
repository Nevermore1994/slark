//
// Created by Nevermore on 2024/4/9.
// slark TimerTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <gtest/gtest.h>
#include <chrono>
#include <format>
#include "TimerManager.h"
#include "Util.hpp"

using namespace slark;

TEST(Timer, delay) {
    using namespace std::chrono_literals;
    auto now = Time::nowTimeStamp();
    TimerManager::shareInstance().runAfter(10ms, [&]{
        auto t = static_cast<double>(Time::nowTimeStamp() - now) / 1000.0;
        std::println("delay {} ms", t);
        ASSERT_GE(t, 10.0);
    });
    std::this_thread::sleep_for(0.5s);
}

TEST(Timer, loop) {
    using namespace std::chrono_literals;
    auto now = Time::nowTimeStamp();
    TimerManager::shareInstance().runLoop(10ms, [&]{
        auto n = Time::nowTimeStamp();
        auto t = ceil(static_cast<double>(n - now) / 1000.0);
        std::println("delay {} ms", t);
        ASSERT_GE(t, 10.0);
        now = n;
    });
    std::this_thread::sleep_for(3s);
}

