//
//  SafeDequeTest.cpp
//  base_test
//
//  Created by Rolf.Tan on 2023/10/17.
//

#include <gtest/gtest.h>
#include <ranges>
#include <thread>
#include <iostream>
#include "SafeDeque.hpp"

using namespace slark;

TEST(update, SafeDeque) {
    SafeDeque<int> deque;
    std::thread t1([&]{
        for(auto i : std::views::iota(0, 10000)){
            deque.push(i);
        }
    });
    
    std::thread t2([&]{
        int count = 0;
        while(count < 10000) {
            if (auto [isSuccess, value] = deque.pop(); isSuccess) {
                ASSERT_EQ(value, count);
                count ++;
            }
        }
    });
    t1.join();
    t2.join();
}

TEST(update1, SafeDeque) {
    SafeDeque<int> deque;
    std::thread t1([&]{
        for(auto i : std::views::iota(0, 10000)){
            deque.push(i);
        }
    });
    
    std::thread t2([&]{
        int count = 0;
        while(count < 10000) {
            if (auto [isSuccess, value] = deque.pop(); isSuccess) {
                ASSERT_EQ(value, count);
                count ++;
            }
        }
    });
    t1.join();
    t2.join();
}

TEST(empty1, SafeDeque) {
    SafeDeque<int> deque;
    auto [isSuccess, value] = deque.pop();
    ASSERT_EQ(deque.empty(), true);
    ASSERT_EQ(isSuccess, false);
}

TEST(empty2, SafeDeque) {
    SafeDeque<int> deque;
    deque.push(1);
    auto [isSuccess, value] = deque.pop();
    ASSERT_EQ(deque.empty(), true);
    ASSERT_EQ(isSuccess, true);
}



