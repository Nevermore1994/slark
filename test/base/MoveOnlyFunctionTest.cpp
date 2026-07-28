//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "MoveOnlyFunction.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <numeric>
#include <string>

using namespace slark;

TEST(MoveOnlyFunction, DefaultConstruction) {
    MoveOnlyFunction<void()> fn;
    EXPECT_FALSE(static_cast<bool>(fn));
}

TEST(MoveOnlyFunction, NullptrConstruction) {
    MoveOnlyFunction<void()> fn(nullptr);
    EXPECT_FALSE(static_cast<bool>(fn));
}

TEST(MoveOnlyFunction, LambdaConstruction) {
    MoveOnlyFunction<int32_t()> fn([]() {
        return 42;
    });
    EXPECT_TRUE(static_cast<bool>(fn));
    EXPECT_EQ(fn(), 42);
}

TEST(MoveOnlyFunction, LambdaWithCapture) {
    int32_t value = 10;
    MoveOnlyFunction<int32_t()> fn([value]() {
        return value * 2;
    });
    EXPECT_EQ(fn(), 20);
}

TEST(MoveOnlyFunction, LambdaWithArgs) {
    MoveOnlyFunction<int32_t(int32_t, int32_t)> fn([](int32_t a, int32_t b) {
        return a + b;
    });
    EXPECT_EQ(fn(3, 4), 7);
}

TEST(MoveOnlyFunction, MoveConstruction) {
    MoveOnlyFunction<int32_t()> fn1([]() {
        return 42;
    });
    MoveOnlyFunction<int32_t()> fn2(std::move(fn1));
    EXPECT_TRUE(static_cast<bool>(fn2));
    EXPECT_EQ(fn2(), 42);
}

TEST(MoveOnlyFunction, MoveAssignment) {
    MoveOnlyFunction<int32_t()> fn1([]() {
        return 42;
    });
    MoveOnlyFunction<int32_t()> fn2;
    fn2 = std::move(fn1);
    EXPECT_TRUE(static_cast<bool>(fn2));
    EXPECT_EQ(fn2(), 42);
}

TEST(MoveOnlyFunction, AssignNullptr) {
    MoveOnlyFunction<int32_t()> fn([]() {
        return 42;
    });
    EXPECT_TRUE(static_cast<bool>(fn));
    fn = nullptr;
    EXPECT_FALSE(static_cast<bool>(fn));
}

TEST(MoveOnlyFunction, MoveOnlyCapture) {
    auto ptr = std::make_unique<int32_t>(42);
    MoveOnlyFunction<int32_t()> fn([p = std::move(ptr)]() {
        return *p;
    });
    EXPECT_EQ(fn(), 42);
}

TEST(MoveOnlyFunction, VoidReturn) {
    int32_t counter = 0;
    MoveOnlyFunction<void()> fn([&counter]() {
        ++counter;
    });
    fn();
    fn();
    EXPECT_EQ(counter, 2);
}

TEST(MoveOnlyFunction, StringReturn) {
    MoveOnlyFunction<std::string(const std::string&)> fn([](const std::string& s) {
        return s + " world";
    });
    EXPECT_EQ(fn("hello"), "hello world");
}

TEST(MoveOnlyFunction, MoveOnlyCaptureChain) {
    auto ptr = std::make_unique<std::string>("hello");
    MoveOnlyFunction<std::string()> fn1([p = std::move(ptr)]() {
        return *p;
    });
    MoveOnlyFunction<std::string()> fn2(std::move(fn1));
    EXPECT_EQ(fn2(), "hello");
}

TEST(MoveOnlyFunction, LargeCapture) {
    std::array<int32_t, 100> arr{};
    std::iota(arr.begin(), arr.end(), 0);
    MoveOnlyFunction<int32_t()> fn([arr]() {
        int32_t sum = 0;
        for (auto v : arr) {
            sum += v;
        }
        return sum;
    });
    EXPECT_EQ(fn(), 4950);
}

TEST(MoveOnlyFunction, ConstCall) {
    const MoveOnlyFunction<int32_t()> fn([]() {
        return 42;
    });
    EXPECT_EQ(fn(), 42);
}
