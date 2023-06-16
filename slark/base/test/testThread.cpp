//
// Created by Nevermore on 2021/10/22.
// slark test/testThread
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "testThread.hpp"
#include "Random.hpp"
#include "Thread.hpp"
#include <unistd.h>
#include <iostream>

using namespace std;
using namespace slark;

void test() {
    uint64_t start = Time::nowTimeStamp();
    cout << "test start" << endl;
    uint64_t sum = 0;
    for (int i = 0; i < 1000000; i++) {
        sum += i;
    }
    cout << "test end, cost time:" << Time::nowTimeStamp() - start << endl;
}

void testThread() {
    int testCount = 5;
    Thread thread("----- test Thread -----");
    thread.setFunc(test);
    while (testCount) {
        cout << "test Thread end" << testCount << "," << Time::nowTimeStamp() << endl;
        thread.resume();
        usleep(1000 * 10);
        thread.pause();
        sleep(2);
        testCount--;
        cout << "test Thread end" << testCount << "," << Time::nowTimeStamp() << endl;
    }
    thread.stop();
    cout << "----- test thread end -----" << endl;
}
