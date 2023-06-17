//
// Created by Nevermore on 2021/10/22.
// slark test/testTimer
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "testTimer.hpp"
#include <iostream>

using namespace std;
using namespace Slark;
using namespace std::chrono;
using namespace chrono_literals;

void testTimer() {
    uint64_t now = Time::nowTimeStamp();
    cout << "test timer start:" << now << endl;
    TimerManager::shareInstance().runAfter(7ms, [&]() {
        cout << "test runAfter first:" << (Time::nowTimeStamp() - now) / 1000 << endl;
    });
    auto t2 = TimerManager::shareInstance().runLoop(100ms, [&]() {
        static int count = 0;
        auto t = Time::nowTimeStamp();
        cout << "test runLoop:" << (t - now) / 1000 << ", count:" << ++count << endl;
        now = t;
    });
    std::this_thread::sleep_for(1000ms);
    TimerManager::shareInstance().cancel(t2);
    cout << "------ test timer t1  end:" << Time::nowTimeStamp() << endl;

    for (int i = 0; i < 100; i++) {
        TimerManager::shareInstance().runAfter(milliseconds(i), [i]() {
            cout << "test runAfter:" << i << " " << Time::nowTimeStamp()  << endl;
            TimerManager::shareInstance().runAfter(milliseconds(i * 10), [i]() {
                cout << "test runAfter2:" << i << " " << Time::nowTimeStamp() << endl;
            });
        });
    }
    std::this_thread::sleep_for(2000ms);
    cout << "------ test timer t2  end:" << Time::nowTimeStamp() << endl;
}