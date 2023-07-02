//
// Created by Nevermore on 2023/7/2.
// slark baseMain
// Copyright (c) 2023 Nevermore All rights reserved.
//

#include <thread>
#include "Log.hpp"
#include "Random.hpp"

using namespace std;
using namespace slark;

int main () {
    thread thead1([]{
        using namespace std::chrono_literals;
        for(int i = 0; i < 10000; i++) {
            LogI("thread1: %s", Random::randomString(32).c_str());
            std::this_thread::sleep_for(10ms);
        }
    });

    thread thead2([]{
        using namespace std::chrono_literals;
        for(int i = 0; i < 10000; i++) {
            LogI("thread2: %s", Random::randomString(32).c_str());
            std::this_thread::sleep_for(10ms);
        }
    });

    thread thead3([]{
        using namespace std::chrono_literals;
        for(int i = 0; i < 10000; i++) {
            LogI("thread3: %s", Random::randomString(32).c_str());
            std::this_thread::sleep_for(10ms);
        }
    });
    thead1.join();
    thead2.join();
    thead3.join();
    return 0;
}

