//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Channel.hpp"

#include <gtest/gtest.h>

#include <numeric>
#include <optional>
#include <string>
#include <thread>

using namespace slark;
using namespace std::literals;

TEST(ChannelTest, CheckType) {
    using type = std::string;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    EXPECT_TRUE((std::is_same_v<GetChannelType<decltype(sp)>, type>));
    EXPECT_TRUE((std::is_same_v<GetChannelType<decltype(rp)>, type>));
    auto ssp = SenderRefPtr<type>(std::move(sp));
    EXPECT_TRUE((std::is_same_v<GetChannelType<decltype(ssp)>, type>));
}

TEST(ChannelTest, PtrSendSingleMessage) {
    using type = int;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    ssp << std::vector<int>{5, 6, 7, 8, 9} << std::deque<int>{10, 11};
    ssp->done();
    t1.join();
}

TEST(ChannelTest, Close) {
    {
        using type = std::string;
        auto _ch = Channel<type>::create();
        auto sp = std::move(std::get<0>(_ch));
        auto rp = std::move(std::get<1>(_ch));
        sp->done();
        EXPECT_EQ(sp->isDone(), true);
        EXPECT_FALSE(sp->send("123"s).has_value());
    }

    {
        using type = std::string;
        auto _ch = Channel<type>::create();
        auto sp = std::move(std::get<0>(_ch));
        auto rp = std::move(std::get<1>(_ch));
        rp.reset();
        EXPECT_EQ(sp->isDone(), false);
        EXPECT_FALSE(sp->send("xyz"s).has_value());
    }

    {
        using type = std::string;
        auto _ch = Channel<type>::create();
        auto sp = std::move(std::get<0>(_ch));
        auto rp = std::move(std::get<1>(_ch));
        sp->done();
        EXPECT_THROW(sp << "123"s, std::runtime_error);
        std::string s;
        EXPECT_THROW(rp >> s, std::runtime_error);
    }
}

TEST(ChannelTest, Range) {
    using type = int;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    sp << std::vector<int>{5, 6, 7, 8, 9} << std::deque<int>{10, 11};
    sp->done();
    t1.join();
}

TEST(ChannelTest, MultiThread) {
    using type = int;
    auto _ch = Channel<type>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    std::thread t([rp = rp.get(), sp = sp.get()] {
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp->done();
    auto res = rp->receive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::Closed);
}

TEST(ChannelTest, EmptyRangeSend) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    std::vector<int> empty;
    EXPECT_TRUE(sp->send(empty));
    EXPECT_EQ(rp->size(), 0u);
    sp->done();
}

TEST(ChannelTest, SendRvalueRange) {
    auto _ch = Channel<std::string>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    std::vector<std::string> data = {"a", "b", "c"};
    EXPECT_TRUE(sp->send(std::move(data)));
    EXPECT_TRUE(rp->receive().has_value());
    EXPECT_TRUE(rp->receive().has_value());
    EXPECT_TRUE(rp->receive().has_value());
    sp->done();
}

TEST(ChannelTest, SizeAndEmpty) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    rp->wakeup();
    EXPECT_FALSE(sp->isDone());
    EXPECT_TRUE(sp->send(1).has_value());
    auto res = rp->tryReceive();
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 1);
    sp->done();
}

TEST(ChannelTest, MultipleWakeups) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    rp->wakeup();
    rp->wakeup();
    auto res = rp->receive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::Awakened);
    sp->done();
}

TEST(ChannelTest, EmplaceAfterClosed) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp->done();
    EXPECT_FALSE(sp->emplace(1));
}

TEST(ChannelTest, SendRvalueRangeWithNonCopyable) {
    auto _ch = Channel<std::unique_ptr<int>>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
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

TEST(ChannelTest, BoundedChannelBasic) {
    auto _ch = Channel<int>::create(3);
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    EXPECT_TRUE(sp->send(1).has_value());
    EXPECT_TRUE(sp->send(2).has_value());
    EXPECT_TRUE(sp->send(3).has_value());
    bool done = false;
    std::thread t([&] {
        sp->send(4);
        done = true;
    });
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(done);
    EXPECT_EQ(*rp->receive(), 1);
    t.join();
    EXPECT_TRUE(done);
    sp->done();
}

TEST(ChannelTest, SendPreservesValueOnClose) {
    auto _ch = Channel<std::string>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp->done();
    auto res = sp->send(std::string("lost"));
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), "lost");
}

TEST(ChannelTest, SendPreservesMovedValueOnClose) {
    auto _ch = Channel<std::unique_ptr<int>>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp->done();
    auto ptr = std::make_unique<int>(42);
    auto res = sp->send(std::move(ptr));
    ASSERT_FALSE(res.has_value());
    ASSERT_TRUE(res.error() != nullptr);
    EXPECT_EQ(*res.error(), 42);
}

TEST(ChannelTest, ReceiverDropFailsSend) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    rp.reset();
    EXPECT_FALSE(sp->isDone());
    auto res = sp->send(1);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), 1);
}

TEST(ChannelTest, BoundedReceiverDropWakesSender) {
    auto _ch = Channel<int>::create(1);
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    EXPECT_TRUE(sp->send(1).has_value());
    bool done = false;
    std::thread t([&] {
        auto res = sp->send(2);
        EXPECT_FALSE(res.has_value());
        EXPECT_EQ(res.error(), 2);
        done = true;
    });
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(done);
    rp.reset();
    t.join();
    EXPECT_TRUE(done);
}

TEST(ChannelTest, PostFireAndForget) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp->post(42);
    EXPECT_EQ(rp->size(), 1u);
    EXPECT_EQ(*rp->receive(), 42);
    sp->done();
    sp->post(99);
}

TEST(ChannelTest, UnboundedSendDoesNotBlock) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    for (int i = 0; i < 10000; i++) {
        EXPECT_TRUE(sp->send(i).has_value());
    }
    EXPECT_EQ(rp->size(), 10000u);
    sp->done();
}

TEST(ChannelTest, PostRvalueMove) {
    auto _ch = Channel<std::string>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    std::string s = "hello";
    sp->post(std::move(s));
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(*rp->receive(), "hello");
    sp->done();
}

TEST(ChannelTest, PostLvalueCopy) {
    auto _ch = Channel<std::string>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    std::string s = "hello";
    sp->post(s);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(*rp->receive(), "hello");
    sp->done();
}

TEST(ChannelTest, PostNonCopyable) {
    auto _ch = Channel<std::unique_ptr<int>>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    auto ptr = std::make_unique<int>(7);
    sp->post(std::move(ptr));
    EXPECT_EQ(ptr, nullptr);
    auto res = rp->receive();
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(**res, 7);
    sp->done();
}

TEST(ChannelTest, PostAfterReceiverDrop) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    {
        auto rp = std::move(std::get<1>(_ch));
        sp->post(1);
        EXPECT_EQ(*rp->receive(), 1);
    }
    sp->post(2);
    sp->post(3);
}

TEST(ChannelTest, PostBoundedBlocksAndDelivers) {
    auto _ch = Channel<int>::create(2);
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp->post(1);
    sp->post(2);
    bool posted = false;
    std::thread t([&] {
        sp->post(3);
        posted = true;
    });
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(posted);
    EXPECT_EQ(*rp->receive(), 1);
    t.join();
    EXPECT_TRUE(posted);
    EXPECT_EQ(*rp->receive(), 2);
    EXPECT_EQ(*rp->receive(), 3);
    sp->done();
}

TEST(ChannelTest, PostBoundedReceiverDropWakesUp) {
    auto _ch = Channel<int>::create(1);
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp->post(1);
    bool posted = false;
    std::thread t([&] {
        sp->post(2);
        posted = true;
    });
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(posted);
    rp.reset();
    t.join();
    EXPECT_TRUE(posted);
}

TEST(ChannelTest, PostAfterDoneNoThrow) {
    auto _ch = Channel<int>::create(3);
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp->done();
    sp->post(1);
    sp->post(2);
    rp.reset();
    sp->post(3);
    sp->post(4);
    SUCCEED();
}

TEST(ChannelTest, DoneIdempotent) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp->done();
    sp->done();
    sp->done();
    auto res = rp->receive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::Closed);
}

TEST(ChannelTest, DoneDrainsRemainingMessages) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(sp->send(i).has_value());
    }
    sp->done();
    for (int i = 0; i < 5; i++) {
        auto res = rp->receive();
        ASSERT_TRUE(res.has_value()) << "i=" << i;
        EXPECT_EQ(*res, i);
    }
    auto res = rp->receive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::Closed);
}

TEST(ChannelTest, DoneWakesBlockedReceiver) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    bool woken = false;
    ChannelEventType ev = ChannelEventType::Unknown;
    std::thread t([&, rp = rp.get()] {
        auto res = rp->receive();
        ASSERT_FALSE(res.has_value());
        ev = res.error();
        woken = true;
    });
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(woken);
    sp->done();
    t.join();
    EXPECT_TRUE(woken);
    EXPECT_EQ(ev, ChannelEventType::Closed);
}

TEST(ChannelTest, DoneWakesBlockedSender) {
    auto _ch = Channel<int>::create(1);
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    EXPECT_TRUE(sp->send(1).has_value());
    bool woken = false;
    int recovered = 0;
    std::thread t([&] {
        auto res = sp->send(2);
        EXPECT_FALSE(res.has_value());
        recovered = res.error();
        woken = true;
    });
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(woken);
    sp->done();
    t.join();
    EXPECT_TRUE(woken);
    EXPECT_EQ(recovered, 2);
}

TEST(ChannelTest, MultipleSendersOneDoneClosesAll) {
    auto _ch = Channel<int>::create();
    auto sp1 = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    auto ssp = SenderRefPtr<int>(std::move(sp1));
    auto sp2 = ssp;

    EXPECT_TRUE(sp2->send(1).has_value());
    EXPECT_EQ(*rp->receive(), 1);

    ssp->done();
    EXPECT_TRUE(ssp->isDone());
    EXPECT_TRUE(sp2->isDone());

    auto r1 = ssp->send(2);
    auto r2 = sp2->send(3);
    EXPECT_FALSE(r1.has_value());
    EXPECT_FALSE(r2.has_value());
    auto res = rp->receive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::Closed);
}

TEST(ChannelTest, DoneAfterReceiverDrop) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    {
        auto rp = std::move(std::get<1>(_ch));
    }
    EXPECT_FALSE(sp->isDone());
    sp->done();
    EXPECT_TRUE(sp->isDone());
    auto res = sp->send(1);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), 1);
}

TEST(ChannelTest, OperatorLeftShiftChain) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp << 1 << 2 << 3 << 4 << 5;
    for (int i = 1; i <= 5; i++) {
        EXPECT_EQ(*rp->receive(), i);
    }
    sp->done();
}

TEST(ChannelTest, OperatorLeftShiftMixSingleAndRange) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp << 1;
    sp << std::vector<int>{2, 3};
    sp << 4;
    sp << std::deque<int>{5, 6, 7};
    for (int i = 1; i <= 7; i++) {
        EXPECT_EQ(*rp->receive(), i);
    }
    sp->done();
}

TEST(ChannelTest, OperatorRightShiftChain) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    std::thread t([rp = std::move(rp)] {
        int t1, t2, t3;
        rp >> t1 >> t2 >> t3;
        EXPECT_EQ(t1, 10);
        EXPECT_EQ(t2, 20);
        EXPECT_EQ(t3, 30);
    });
    sp << 10 << 20 << 30;
    sp->done();
    t.join();
}

TEST(ChannelTest, ReceiverCopySharesChannel) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    Receiver<int> rcv(*std::get<1>(_ch));
    auto rcv2 = rcv;

    sp->post(1);
    EXPECT_EQ(rcv.size(), 1u);
    EXPECT_EQ(rcv2.size(), 1u);

    EXPECT_EQ(*rcv.receive(), 1);
    EXPECT_EQ(rcv.size(), 0u);
    EXPECT_EQ(rcv2.size(), 0u);

    sp->post(2);
    EXPECT_EQ(*rcv2.receive(), 2);
    sp->done();

    auto res = rcv.receive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::Closed);
    res = rcv2.receive();
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ChannelEventType::Closed);
}

TEST(ChannelTest, ReceiverMoveTransfersOwnership) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp1 = std::move(std::get<1>(_ch));
    auto rp2 = std::move(rp1);

    sp->post(42);
    EXPECT_EQ(*rp2->receive(), 42);
    sp->done();
}

TEST(ChannelTest, MultipleReceiversAliveCount) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rcv = std::make_optional<Receiver<int>>(std::move(*std::get<1>(_ch)));
    std::get<1>(_ch).reset();
    std::optional<Receiver<int>> rcv2 = *rcv;
    EXPECT_TRUE(sp->send(1).has_value());
    rcv2.reset();
    EXPECT_TRUE(sp->send(2).has_value());
    rcv.reset();
    EXPECT_FALSE(sp->send(3).has_value());
}

TEST(ChannelTest, TryReceiveAllOnEmpty) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    auto msgs = rp->tryReceiveAll();
    EXPECT_TRUE(msgs.empty());
    sp->done();
    msgs = rp->tryReceiveAll();
    EXPECT_TRUE(msgs.empty());
}

TEST(ChannelTest, BoundedSendRangeDoesNotBlock) {
    auto _ch = Channel<int>::create(2);
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    std::vector<int> data = {1, 2, 3, 4, 5};
    EXPECT_TRUE(sp->send(data));
    EXPECT_EQ(rp->size(), 5u);
    for (int i = 1; i <= 5; i++) {
        EXPECT_EQ(*rp->receive(), i);
    }
    sp->done();
}

TEST(ChannelTest, PostIteratorPostIncrement) {
    auto _ch = Channel<int>::create();
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    sp << 1 << 2 << 3;
    auto it = rp->begin();
    EXPECT_EQ(*it, 1);
    auto prev = it++;
    EXPECT_EQ(*prev, 1);
    EXPECT_EQ(*it, 2);
    ++it;
    EXPECT_EQ(*it, 3);
    sp->done();
    ++it;
    EXPECT_EQ(it, rp->end());
}

TEST(ChannelTest, ConcurrentBoundedMultiProducer) {
    auto _ch = Channel<int>::create(4);
    auto sp = std::move(std::get<0>(_ch));
    auto rp = std::move(std::get<1>(_ch));
    auto ssp = SenderRefPtr<int>(std::move(sp));

    std::atomic<int> counter{0};
    auto producer = [ssp, &counter](int start) {
        for (int i = start; i < start + 50; i++) {
            ssp->post(i);
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    };
    std::thread p1(producer, 0);
    std::thread p2(producer, 50);
    std::thread p3(producer, 100);

    int received = 0;
    while (received < 150) {
        auto res = rp->receive();
        ASSERT_TRUE(res.has_value()) << "received=" << received;
        received++;
    }
    ssp->done();
    p1.join();
    p2.join();
    p3.join();
}