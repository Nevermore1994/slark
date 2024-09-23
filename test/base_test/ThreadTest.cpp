//
// Created by Nevermore on 2024/4/2.
// slark ThreadTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <gtest/gtest.h>
#include <format>
#include "Thread.h"
#include "Util.hpp"

using namespace slark;

TEST(Thread, state) {
    int x = 0;
    Thread thread(Util::genRandomName("thread"), [&](){
        x++;
    });
    thread.start();
    ASSERT_EQ(thread.isRunning(), true);
    ASSERT_EQ(thread.isExit(), false);
    thread.pause();
    ASSERT_EQ(thread.isRunning(), false);
    ASSERT_EQ(thread.isExit(), false);
    thread.resume();
    ASSERT_EQ(thread.isRunning(), true);
    ASSERT_EQ(thread.isExit(), false);
    thread.pause();
    ASSERT_EQ(thread.isRunning(), false);
    ASSERT_EQ(thread.isExit(), false);
    thread.pause();
    ASSERT_EQ(thread.isRunning(), false);
    ASSERT_EQ(thread.isExit(), false);
    thread.stop();
    ASSERT_EQ(thread.isRunning(), false);
    ASSERT_EQ(thread.isExit(), true);
}

TEST(Thread, timer) {
    using namespace std::chrono;
    int x = 0;
    Thread thread(Util::genRandomName("thread"), [&](){
        x++;
    });
    thread.start();
    auto t = Time::nowTimeStamp();
    thread.runAfter(10ms, [&t]{
        auto time = Time::nowTimeStamp() - t;
        ASSERT_GT(time, 10 * 1000);
        std::println("delay us: {}", time.toMicroSeconds());
    });
    std::this_thread::sleep_for(20ms);
}