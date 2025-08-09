//
// Created by Nevermore on 2024/9/15.
// slark ClockTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "Clock.h"  // Replace with the correct header for the Clock class

using namespace std::chrono;
using namespace slark;

TEST(ClockTest, InitialState) {
    Clock clock;
    EXPECT_EQ(clock.time(), 0);
}

TEST(ClockTest, SetTime) {
    Clock clock;
    clock.setTime(Time::TimePoint::fromMilliSeconds(5000ms));
    clock.start();
    std::this_thread::sleep_for(milliseconds(2000));
    EXPECT_GE(clock.time().toMilliSeconds(), milliseconds(7000));
    EXPECT_LE(clock.time().toMilliSeconds(), milliseconds(7500));
}

TEST(ClockTest, StartPauseReset) {
    Clock clock;
    clock.setTime(Time::TimePoint::fromMilliSeconds(5000ms));
    clock.start();
    std::this_thread::sleep_for(milliseconds(2000));//7000
    EXPECT_GE(clock.time().toMilliSeconds(), milliseconds(6500));
    clock.pause();//7000
    EXPECT_GE(clock.time().toMilliSeconds(), milliseconds(6500));
    std::this_thread::sleep_for(milliseconds(1000));//7000
    EXPECT_GE(clock.time().toMilliSeconds(), milliseconds(6500));
    clock.start();
    std::this_thread::sleep_for(milliseconds(1000));
    EXPECT_GE(clock.time().toMilliSeconds(), milliseconds(7500));
    EXPECT_LE(clock.time().toMilliSeconds(), milliseconds(8500));
    clock.reset();
    EXPECT_EQ(clock.time(), 0);
}

TEST(ClockTest, MultiThreadedStartPause) {
    Clock clock;
    clock.start();
    std::thread pauseThread([&]() {
        std::this_thread::sleep_for(milliseconds(2000));
        clock.pause();
        EXPECT_GE(clock.time().toMilliSeconds(), milliseconds(990));
    });
    std::thread resumeThread([&]() {
        std::this_thread::sleep_for(milliseconds(1000));
        EXPECT_GE(clock.time().toMilliSeconds(), milliseconds(990));
        clock.start(); //reset start
    });

    pauseThread.join();
    resumeThread.join();

    std::this_thread::sleep_for(milliseconds(1000));
    EXPECT_GE(clock.time().toMilliSeconds(), milliseconds(990));
}

TEST(ClockTest, MultiThreadedReset) {
    Clock clock;
    clock.start();
    std::thread resetThread([&]() {
        std::this_thread::sleep_for(milliseconds(2000));
        clock.reset();
    });
    resetThread.join();
    EXPECT_EQ(clock.time(), 0);
}