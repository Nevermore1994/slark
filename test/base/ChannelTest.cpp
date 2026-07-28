//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Channel.hpp"

#include <gtest/gtest.h>
#include <numeric>
#include <string>
#include <thread>

using namespace slark;
using namespace std::literals;

TEST(ChannelTest, CheckType) {
    using type = std::string;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    EXPECT_TRUE((std::is_same_v<GetChannelType<decltype(sp)>, type>));
    EXPECT_TRUE((std::is_same_v<GetChannelType<decltype(rp)>, type>));
    auto ssp = SenderRefPtr<type>(std::move(sp));
    EXPECT_TRUE((std::is_same_v<GetChannelType<decltype(ssp)>, type>));
}

TEST(ChannelTest, PtrSendSingleMessage) {
    using type = int;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::thread t1([rp = std::move(rp)] {
        type value = 0;
        int t1, t2;
        rp >> t1 >> t2;
        EXPECT_EQ(t1, value);
        value++;
        EXPECT_EQ(t2, value);
        value++;
        for (;;) {
            if (auto res = rp->receive(); res.has_value()) {
                EXPECT_EQ(*res, value);
                value++;
            } else {
                break;
            }
        }
    });
    for (std::weakly_incrementable auto i : std::views::iota(0, 10)) {
        sp << i;
    }
    sp->done();
    t1.join();
}

TEST(ChannelTest, Receive) {
    using type = std::string;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::thread t1([rp = std::move(rp)] {
        int value = 0;
        for (;;) {
            if (auto res = rp->receive(); res.has_value()) {
                EXPECT_EQ(*res, std::to_string(value));
                value++;
            } else {
                break;
            }
        }
    });
    for (std::weakly_incrementable auto i : std::views::iota(0, 10)) {
        sp << std::to_string(i);
    }
    sp->done();
    t1.join();
}

TEST(ChannelTest, PtrSendMultiMessage) {
    using type = int;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::thread t1([rp = std::move(rp)] {
        type value = 0;
        for (;;) {
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
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    auto ssp = SenderRefPtr<type>(std::move(sp));
    std::thread t1([rp = std::move(rp)] {
        type value = 0;
        for (;;) {
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
        auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
        sp->done();
        EXPECT_EQ(sp->isDone(), true);
        EXPECT_FALSE(sp->send("123"s));
    }

    {
        using type = std::string;
        auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
        rp.reset();
        EXPECT_EQ(sp->isDone(), true);
        EXPECT_FALSE(sp->send("xyz"s));
    }

    {
        using type = std::string;
        auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
        sp->done();
        EXPECT_THROW(sp << "123"s, std::runtime_error);
        std::string s;
        EXPECT_THROW(rp >> s, std::runtime_error);
    }
}

TEST(ChannelTest, Range) {
    using type = int;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::thread t1([rp = std::move(rp)] {
        type value = 0;
        for (auto res : *rp) {
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
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    auto ssp = SenderRefPtr<type>(std::move(sp));
    std::condition_variable cond;
    std::mutex mutex;
    int count = 0;
    std::thread t1([ssp, &cond, &mutex, &count] {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [&count, &ssp] {
                return (count % 3) == 0 || ssp->isDone();
            });
            if (ssp->isDone()) {
                // std::cout << "--- MultiThread test t1 exit ---" << std::endl;
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
    std::thread t2([ssp, &cond, &mutex, &count] {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [&count, &ssp] {
                return (count % 3) == 1 || ssp->isDone();
            });
            if (ssp->isDone()) {
                // std::cout << "--- MultiThread test t2 exit ---" << std::endl;
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
    std::thread t3([ssp, &cond, &mutex, &count] {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [&count, &ssp] {
                return (count % 3) == 2 || ssp->isDone();
            });
            if (ssp->isDone()) {
                // std::cout << "--- MultiThread test t3 exit ---" << std::endl;
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
    std::thread t4([rp = std::move(rp)] {
        type value = 0;
        for (;;) {
            if (auto res = rp->tryReceive(); res.has_value()) {
                EXPECT_EQ(*res, value);
                value++;
            } else if (res.error() == ChannelEventType::Closed) {
                break;
            }
        }
        // std::cout << "--- MultiThread test t4 exit ---" << std::endl;
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

    explicit A(int value_) : value(value_) {}
};

struct TestValue : public A {
    TestValue() = default;
    ~TestValue() override = default;

    explicit TestValue(const std::string& key_) : key(key_) {}

    TestValue(TestValue&& rhs) noexcept : A(rhs.value), key(std::move(rhs.key)) {}

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
    auto _ch = Channel<TestValue>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::thread t1([rp = std::move(rp)] {
        int value = 0;
        for (auto& res : *rp) {
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
    auto _ch = Channel<type*>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::thread t1([rp = std::move(rp)] {
        int value = 0;
        for (auto res : *rp) {
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

TEST(ChannelTest, Emplace) {
    using type = std::pair<int, std::string>;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::thread t([rp = std::move(rp)] {
        for (int i = 0;; i++) {
            if (auto res = rp->receive(); res.has_value()) {
                EXPECT_EQ(res->first, i);
                EXPECT_EQ(res->second, std::to_string(i));
            } else {
                break;
            }
        }
    });
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(sp->emplace(i, std::to_string(i)));
    }
    sp->done();
    t.join();
}

TEST(ChannelTest, TryReceiveAll) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    for (int i = 0; i < 10; i++) {
        sp << i;
    }
    auto msgs = rp->tryReceiveAll();
    EXPECT_EQ(msgs.size(), 10u);
    int expected = 0;
    for (auto& v : msgs) {
        EXPECT_EQ(v, expected++);
    }
    EXPECT_TRUE(rp->empty());
    EXPECT_EQ(rp->size(), 0u);
}

TEST(ChannelTest, Wakeup) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::thread t([rp = rp.get()] {
        auto res = rp->receive();
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error(), ChannelEventType::Awakened);
    });
    std::this_thread::sleep_for(10ms);
    rp->wakeup();
    t.join();
}

TEST(ChannelTest, WakeupThenDataThenClosed) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::thread t([rp = rp.get(), sp = sp.get()] {
        // wakeup 后应该能继续正常接收数据
        auto res = rp->receive();
        ASSERT_FALSE(res.has_value());
        EXPECT_EQ(res.error(), ChannelEventType::Awakened);
        res = rp->receive();
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(*res, 42);
        sp->done();
    });
    std::this_thread::sleep_for(10ms);
    rp->wakeup();
    std::this_thread::sleep_for(10ms);
    sp << 42;
    t.join();
}

TEST(ChannelTest, TryReceiveOnEmpty) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    auto res = rp->tryReceive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::None);
    sp << 1;
    res = rp->tryReceive();
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 1);
}

TEST(ChannelTest, ReceiveOnClosedEmpty) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    sp->done();
    auto res = rp->receive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::Closed);
}

TEST(ChannelTest, EmptyRangeSend) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::vector<int> empty;
    EXPECT_TRUE(sp->send(empty));
    EXPECT_EQ(rp->size(), 0u);
    sp->done();
}

TEST(ChannelTest, SendRvalueRange) {
    auto _ch = Channel<std::string>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::vector<std::string> data = {"a", "b", "c"};
    // std::move(data) 匹配 const I& 重载，元素被拷贝而非移动
    // 此处仅验证 send 成功、数据正确送达
    EXPECT_TRUE(sp->send(std::move(data)));
    EXPECT_TRUE(rp->receive().has_value());
    EXPECT_TRUE(rp->receive().has_value());
    EXPECT_TRUE(rp->receive().has_value());
    sp->done();
}

TEST(ChannelTest, SizeAndEmpty) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    EXPECT_TRUE(rp->empty());
    EXPECT_EQ(rp->size(), 0u);
    sp << 1;
    EXPECT_FALSE(rp->empty());
    EXPECT_EQ(rp->size(), 1u);
    sp << 2 << 3;
    EXPECT_EQ(rp->size(), 3u);
    (void)rp->tryReceiveAll();
    EXPECT_TRUE(rp->empty());
    EXPECT_EQ(rp->size(), 0u);
    sp->done();
}

TEST(ChannelTest, WakeupDoesNotClose) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    rp->wakeup();
    EXPECT_FALSE(sp->isDone());
    EXPECT_TRUE(sp->send(1));
    auto res = rp->tryReceive();
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 1);
    sp->done();
}

TEST(ChannelTest, MultipleWakeups) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    rp->wakeup();
    rp->wakeup(); // 第二次 wakeup 覆盖第一次
    // 只消费一次 Awakened
    auto res = rp->receive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::Awakened);
    sp->done();
}

TEST(ChannelTest, EmplaceAfterClosed) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    sp->done();
    EXPECT_FALSE(sp->emplace(1));
}

TEST(ChannelTest, SendRvalueRangeWithNonCopyable) {
    auto _ch = Channel<std::unique_ptr<int>>::create();
    auto sp = std::move(std::get<0>(_ch)); auto rp = std::move(std::get<1>(_ch));
    std::vector<std::unique_ptr<int>> data;
    data.push_back(std::make_unique<int>(1));
    data.push_back(std::make_unique<int>(2));
    EXPECT_TRUE(sp->send(std::move(data)));
    auto res1 = rp->receive();
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(**res1, 1);
    auto res2 = rp->receive();
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(**res2, 2);
    sp->done();
}