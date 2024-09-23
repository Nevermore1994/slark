//
// Created by Nevermore on 2024/3/28.
// slark ChannelTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <numeric>
#include "Channel.hpp"

using namespace slark;
using namespace std::literals;

TEST(ChannelTest, CheckType) {
    using type = std::string;
    auto [sp, rp] = Channel<type>::create();
    EXPECT_TRUE((std::is_same_v<GetChannelType<decltype(sp)>, type>));
    EXPECT_TRUE((std::is_same_v<GetChannelType<decltype(rp)>, type>));
    auto ssp = SenderRefPtr<type>(std::move(sp));
    EXPECT_TRUE((std::is_same_v<GetChannelType<decltype(ssp)>, type>));
}

TEST(ChannelTest, PtrSendSingleMessage) {
    using type = int;
    auto [sp, rp] = Channel<type>::create();
    std::thread t1([rp = std::move(rp)]{
        type value = 0;
        int t1, t2;
        rp >> t1 >> t2;
        EXPECT_EQ(t1, value);
        value++;
        EXPECT_EQ(t2, value);
        value++;
        for(;;) {
            if (auto res = rp->receive(); res.has_value()) {
                EXPECT_EQ(*res, value);
                value++;
            } else {
                break;
            }
        }
    });
    for (std::weakly_incrementable auto i : std::views::iota(0,10)) {
    sp << i;
    }
    sp->done();
    t1.join();
}

TEST(ChannelTest, Receive) {
    using type = std::string;
    auto [sp, rp] = Channel<type>::create();
    std::thread t1([rp = std::move(rp)]{
        int value = 0;
        for(;;) {
            if (auto res = rp->receive(); res.has_value()) {
                EXPECT_EQ(*res, std::to_string(value));
                value++;
            } else {
                break;
            }
        }
    });
    for(std::weakly_incrementable auto i : std::views::iota(0, 10)) {
    sp << std::to_string(i);
    }
    sp->done();
    t1.join();
}

TEST(ChannelTest, PtrSendMultiMessage) {
    using type = int;
    auto [sp, rp] = Channel<type>::create();
    std::thread t1([rp = std::move(rp)]{
        type value = 0;
        for(;;) {
            if (auto res = rp->receive(); res.has_value()) {
                EXPECT_EQ(*res, value);
                value++;
            } else {
                break;
            }
        }
    });
    std::vector<type> nums(10);
    std::iota(nums.begin(), nums.end(), 0);
    nums | std::views::take(5) | std::views::sender(sp);
    sp << std::vector<int>{5, 6, 7, 8, 9} << std::vector<int>{10, 11};
    sp->done();
    t1.join();
}

TEST(ChannelTest, RefPtrSendMultiMessage) {
    using type = int;
    auto [sp, rp] = Channel<type>::create();
    auto ssp = SenderRefPtr<type>(std::move(sp));
    std::thread t1([rp = std::move(rp)]{
        type value = 0;
        for(;;) {
            if (auto res = rp->receive(); res.has_value()) {
                EXPECT_EQ(*res, value);
                value++;
            } else {
                break;
            }
        }
    });
    std::vector<type> nums(10);
    std::iota(nums.begin(), nums.end(), 0);
    nums | std::views::take(5) | std::views::sender(ssp);
    ssp << std::vector<int>{5, 6, 7, 8, 9} << std::list<int>{10, 11};
    ssp->done();
    t1.join();
}

TEST(ChannelTest, Close) {
    {
        using type = std::string;
        auto [sp, rp] = Channel<type>::create();
        sp->done();
        EXPECT_EQ(sp->isDone(), true);
        EXPECT_FALSE(sp->send("123"s));
    }

    {
        using type = std::string;
        auto [sp, rp] = Channel<type>::create();
        rp.reset();
        EXPECT_EQ(sp->isDone(), true);
        EXPECT_FALSE(sp->send("xyz"s));
    }

    {
        using type = std::string;
        auto [sp, rp] = Channel<type>::create();
        sp->done();
        EXPECT_THROW(sp << "123"s, std::runtime_error);
        std::string s;
        EXPECT_THROW(rp >> s, std::runtime_error);
    }
}

TEST(ChannelTest, Range) {
    using type = int;
    auto [sp, rp] = Channel<type>::create();
    std::thread t1([rp = std::move(rp)]{
        type value = 0;
        for(auto res : *rp) {
            EXPECT_EQ(res, value);
            value++;
        }
    });
    std::vector<type> nums(10);
    std::iota(nums.begin(), nums.end(), 0);
    nums | std::views::take(5) | std::views::sender(sp);
    sp << std::vector<int>{5, 6, 7, 8, 9} << std::list<int>{10, 11};
    sp->done();
    t1.join();
}

TEST(ChannelTest, MultiThread) {
    using type = int;
    auto [sp, rp] = Channel<type>::create();
    auto ssp = SenderRefPtr<type> (std::move(sp));
    std::condition_variable cond;
    std::mutex mutex;
    int count = 0;
    std::thread t1([ssp, &cond, &mutex, &count]{
        for(;;) {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [&count, &ssp]{
                return (count % 3) == 0 || ssp->isDone();
            });
            if (ssp->isDone()) {
                //std::cout << "--- MultiThread test t1 exit ---" << std::endl;
                return;
            }
            ssp << count;
            count++;
            if (count > 99) {
                ssp->done();
            }
            cond.notify_all();
        }
    });
    std::thread t2([ssp, &cond, &mutex, &count]{
        for(;;) {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [&count, &ssp]{
                return (count % 3) == 1 || ssp->isDone();
            });
            if (ssp->isDone()) {
                //std::cout << "--- MultiThread test t2 exit ---" << std::endl;
                return;
            }
            ssp << count;
            count++;
            if (count > 99) {
                ssp->done();
            }
            cond.notify_all();
        }
    });
    std::thread t3([ssp, &cond, &mutex, &count]{
        for(;;) {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [&count, &ssp]{
                return (count % 3) == 2 || ssp->isDone();
            });
            if (ssp->isDone()) {
                //std::cout << "--- MultiThread test t3 exit ---" << std::endl;
                return;
            }
            ssp << count;
            count++;
            if (count > 99) {
                ssp->done();
            }
            cond.notify_all();
        }
    });
    std::thread t4([rp = std::move(rp)]{
    type value = 0;
        for(;;) {
            if (auto res = rp->tryReceive(); res.has_value()) {
                EXPECT_EQ(*res, value);
                value++;
            } else if (res.error() == ChannelEventType::Closed){
                break;
            }
        }
        //std::cout << "--- MultiThread test t4 exit ---" << std::endl;
    });
    t1.join();
    t2.join();
    t3.join();
    t4.join();
}

struct A {
    int value = 0;
    virtual ~A() = default;
    A() = default;
    explicit A(int value_)
    : value(value_) {

    }
};

struct TestValue: public A {
    TestValue() = default;
    ~TestValue() override = default;
    explicit TestValue(const std::string& key_)
    : key(key_) {

    }

    TestValue(TestValue&& rhs) noexcept
    : A(rhs.value)
      , key(std::move(rhs.key)) {

    }

    TestValue& operator=(TestValue&& rhs) noexcept {
        if (&rhs == this) {
            return *this;
        }
        value = rhs.value;
        key = std::move(rhs.key);
        return *this;
    }
    TestValue(const TestValue&) = default;
    TestValue& operator=(const TestValue& rhs) = default;
    std::string key = "key";
};

TEST(ChannelTest, BitwiseCopy) {
    auto [sp, rp] = Channel<TestValue>::create();
    std::thread t1([rp = std::move(rp)]{
        int value = 0;
        for(auto& res : *rp) {
            EXPECT_EQ(res.value, value);
            EXPECT_EQ(res.key, std::to_string(value));
            value++;
        }
    });
    TestValue a;
    a.key = std::to_string(0);
    sp << std::move(a);
    sp->done();
    t1.join();
}

TEST(ChannelTest, ImplicitConversion) {
    using type = A;
    auto [sp, rp] = Channel<type*>::create();
    std::thread t1([rp = std::move(rp)]{
        int value = 0;
        for(auto res : *rp) {
            auto k = static_cast<TestValue*>(res);
            EXPECT_EQ(k->value, value);
            EXPECT_EQ(k->key, std::to_string(value));
            value++;
            delete res;
        }
    });
    std::vector<TestValue*> values;
    auto value = new TestValue();
    value->value = 0;
    value->key = std::to_string(0);
    sp << value;
    for (int i = 1; i < 10; i++) {
    auto tmepValue = new TestValue();
    tmepValue->value = i;
    tmepValue->key = std::to_string(i);
    values.emplace_back(tmepValue);
    }
    sp << std::move(values);
    sp->done();
    t1.join();
}