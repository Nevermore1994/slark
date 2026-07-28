//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Manager.hpp"

#include <gtest/gtest.h>

#include <latch>
#include <string>
#include <thread>
#include <vector>

using namespace slark;

namespace {

struct TestItem {
    std::string name;
    int32_t value = 0;

    TestItem() = default;

    TestItem(std::string n, int32_t v) : name(std::move(n)), value(v) {}
};

}  // namespace

TEST(Manager, AddAndFind) {
    auto& mgr = Manager<TestItem>::shareInstance();
    auto item = std::make_shared<TestItem>("item1", 42);
    mgr.add("id1", item);

    auto found = mgr.find("id1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "item1");
    EXPECT_EQ(found->value, 42);

    mgr.remove("id1");
}

TEST(Manager, FindNonExistent) {
    auto& mgr = Manager<TestItem>::shareInstance();
    auto found = mgr.find("non_existent_id");
    EXPECT_EQ(found, nullptr);
}

TEST(Manager, Remove) {
    auto& mgr = Manager<TestItem>::shareInstance();
    auto item = std::make_shared<TestItem>("item2", 100);
    mgr.add("id2", item);

    mgr.remove("id2");
    auto found = mgr.find("id2");
    EXPECT_EQ(found, nullptr);
}

TEST(Manager, RemoveNonExistent) {
    auto& mgr = Manager<TestItem>::shareInstance();
    mgr.remove("non_existent_remove");
}

TEST(Manager, OverwriteExisting) {
    auto& mgr = Manager<TestItem>::shareInstance();
    auto item1 = std::make_shared<TestItem>("first", 1);
    auto item2 = std::make_shared<TestItem>("second", 2);

    mgr.add("overwrite_id", item1);
    mgr.add("overwrite_id", item2);

    auto found = mgr.find("overwrite_id");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "second");
    EXPECT_EQ(found->value, 2);

    mgr.remove("overwrite_id");
}

TEST(Manager, SingletonInstance) {
    auto& inst1 = Manager<TestItem>::shareInstance();
    auto& inst2 = Manager<TestItem>::shareInstance();
    EXPECT_EQ(&inst1, &inst2);
}

TEST(Manager, MultipleItems) {
    auto& mgr = Manager<TestItem>::shareInstance();
    for (int32_t i = 0; i < 10; ++i) {
        auto id = "multi_" + std::to_string(i);
        mgr.add(id, std::make_shared<TestItem>(id, i));
    }

    for (int32_t i = 0; i < 10; ++i) {
        auto id = "multi_" + std::to_string(i);
        auto found = mgr.find(id);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->value, i);
    }

    for (int32_t i = 0; i < 10; ++i) {
        mgr.remove("multi_" + std::to_string(i));
    }
}

TEST(Manager, ConcurrentAccess) {
    auto& mgr = Manager<TestItem>::shareInstance();
    constexpr int32_t kThreads = 4;
    constexpr int32_t kItemsPerThread = 50;
    std::latch startLatch(kThreads);

    std::vector<std::jthread> threads;
    for (int32_t t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            startLatch.arrive_and_wait();
            for (int32_t i = 0; i < kItemsPerThread; ++i) {
                auto id = "concurrent_" + std::to_string(t) + "_" + std::to_string(i);
                mgr.add(id, std::make_shared<TestItem>(id, t * kItemsPerThread + i));
            }
        });
    }
    threads.clear();

    for (int32_t t = 0; t < kThreads; ++t) {
        for (int32_t i = 0; i < kItemsPerThread; ++i) {
            auto id = "concurrent_" + std::to_string(t) + "_" + std::to_string(i);
            auto found = mgr.find(id);
            ASSERT_NE(found, nullptr) << "Missing: " << id;
            mgr.remove(id);
        }
    }
}
